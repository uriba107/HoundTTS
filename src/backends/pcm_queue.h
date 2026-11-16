#pragma once

#ifndef HOUNDTTS_PCM_QUEUE_H
#define HOUNDTTS_PCM_QUEUE_H

#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <exception>

namespace HoundTTS {

// Thread-safe queue of 16kHz mono 16-bit PCM sample chunks.
// Producer calls Push() then MarkDone() when finished.
// Consumer calls Pop() which blocks until a chunk is available or done.
//
// Subclassing note: any override of PCMQueue::MarkDone() MUST ultimately
// cause m_done to be set and m_cv to be notified — either by calling
// PCMQueue::MarkDone() at the end of the override, or by replicating its
// body. Otherwise consumers blocked in PCMQueue::Pop() will never wake up
// once the producer stops pushing chunks. See CachingPCMQueue::MarkDone
// for an example that forwards completion to a downstream PCMQueue.
class PCMQueue {
public:
    PCMQueue() : m_done(false) {}
    virtual ~PCMQueue() = default;

    virtual void Push(std::vector<int16_t> chunk) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(std::move(chunk));
        }
        m_cv.notify_one();
    }

    // Returns chunk, or empty vector if timed out.
    // Sets *done=true when producer has finished AND queue is empty.
    std::vector<int16_t> Pop(int timeoutMs, bool* done) {
        if (!done) return {}; // caller contract: `done` is required
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !m_queue.empty() || m_done; });
        }
        if (!m_queue.empty()) {
            auto chunk = std::move(m_queue.front());
            m_queue.pop_front();
            *done = false;
            return chunk;
        }
        *done = m_done;
        return {};
    }

    virtual void MarkDone() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
    }

    bool IsDone() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_done && m_queue.empty();
    }

private:
    std::deque<std::vector<int16_t>> m_queue;
    std::mutex                       m_mutex;
    std::condition_variable          m_cv;
    bool                             m_done;
};

// Silence padding: adjust PTT_PAD_SEC to change lead-in / tail globally.
constexpr double PTT_PAD_SEC     = 0.2;
constexpr int    PTT_PAD_SAMPLES = static_cast<int>(PTT_PAD_SEC * 16000);

// RAII completion guarantee: MarkDone() on every exit path, including an
// exception thrown mid-synthesis. MarkDone is idempotent on every queue in the
// Padded -> Caching -> PCM chain, so a provider that also calls it explicitly
// (failure paths) does not duplicate anything.
class ScopedQueueDone {
public:
    explicit ScopedQueueDone(PCMQueue& queue) noexcept : queue_(&queue) {}
    ~ScopedQueueDone() noexcept {
        try {
            if (queue_) queue_->MarkDone();
        } catch (...) {
            // A throwing destructor would call std::terminate.
        }
    }
    ScopedQueueDone(const ScopedQueueDone&) = delete;
    ScopedQueueDone& operator=(const ScopedQueueDone&) = delete;

private:
    PCMQueue* queue_;
};

// ---------------------------------------------------------------------------
// PaddedPCMQueue — wrapper that injects silence before and after the real
// audio to simulate a PTT lead-in / tail.  The downstream queue (and any
// CachingPCMQueue above it) sees the silence as regular PCM, so cached
// entries include the padding.
//
// Nesting is one-way: PaddedPCMQueue must be the OUTER wrapper (pad → cache →
// PCMQueue). Both wrappers hold their own mutex across the downstream forward,
// so nesting them the other way inverts the lock order (opMutex_ -> accMutex_
// here vs accMutex_ -> opMutex_ in CachingPCMQueue::Push) and can deadlock.
// See the lock-order note on CachingPCMQueue in pcm_cache.h.
// ---------------------------------------------------------------------------
class PaddedPCMQueue : public PCMQueue {
public:
    PaddedPCMQueue(std::shared_ptr<PCMQueue> downstream, int padSamples = PTT_PAD_SAMPLES)
        : downstream_(std::move(downstream)), padSamples_(padSamples) {
        // Fail fast on a null downstream / negative pad: both would otherwise
        // surface later as a crash inside Push()/MarkDone() on a detached thread.
        if (!downstream_)
            throw std::invalid_argument("PaddedPCMQueue: downstream cannot be null");
        if (padSamples_ < 0)
            throw std::invalid_argument("PaddedPCMQueue: padSamples must be non-negative");
    }

    void Push(std::vector<int16_t> chunk) override {
        // Push and MarkDone may arrive from different threads (e.g. a killed
        // session signalling completion while the producer still pushes).
        // Serialize them so tail silence/completion cannot overtake a chunk.
        std::lock_guard<std::mutex> lock(opMutex_);
        if (doneSent_.load(std::memory_order_acquire)) return;  // late push after completion
        // Latch only once the lead-in actually reached the downstream queue: a
        // throwing forward must still get its 200ms PTT lead-in on a retry.
        if (!leadInSent_.load(std::memory_order_acquire)) {
            downstream_->Push(std::vector<int16_t>(padSamples_, 0));
            leadInSent_.store(true, std::memory_order_release);
        }
        downstream_->Push(std::move(chunk));
    }

    void MarkDone() override {
        std::exception_ptr ep;
        {
            std::lock_guard<std::mutex> lock(opMutex_);
            if (!doneSent_.exchange(true)) {
                try {
                    ForwardDone_NoLock();
                } catch (...) {
                    // Forwarding failed: best-effort one immediate retry, then
                    // re-arm the latch so a later MarkDone() cannot re-forward.
                    ep = std::current_exception();
                    doneSent_.store(false);
                    try {
                        ForwardDone_NoLock();
                        doneSent_.store(true, std::memory_order_release);
                    } catch (...) {}
                }
            }
            // Always complete this queue locally, even if the forward failed.
            try {
                PCMQueue::MarkDone();
            } catch (...) {
                if (!ep) ep = std::current_exception();
            }
        }
        if (ep) std::rethrow_exception(ep);
    }

private:
    // Tail-silence push + downstream completion. Idempotent per step, so a
    // retry after a partially successful attempt does not duplicate the pad.
    void ForwardDone_NoLock() {
        if (leadInSent_.load(std::memory_order_acquire) &&
            !tailSent_.load(std::memory_order_acquire)) {
            downstream_->Push(std::vector<int16_t>(padSamples_, 0));
            tailSent_.store(true, std::memory_order_release);
        }
        downstream_->MarkDone();
    }

    std::shared_ptr<PCMQueue> downstream_;
    int padSamples_;
    std::mutex opMutex_;   // serializes Push/MarkDone forwarding
    std::atomic<bool> leadInSent_{false};
    std::atomic<bool> tailSent_{false};
    std::atomic<bool> doneSent_{false};
};

} // namespace HoundTTS

#endif // HOUNDTTS_PCM_QUEUE_H
