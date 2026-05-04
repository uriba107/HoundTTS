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

// ---------------------------------------------------------------------------
// PaddedPCMQueue — wrapper that injects silence before and after the real
// audio to simulate a PTT lead-in / tail.  The downstream queue (and any
// CachingPCMQueue above it) sees the silence as regular PCM, so cached
// entries include the padding.
// ---------------------------------------------------------------------------
class PaddedPCMQueue : public PCMQueue {
public:
    PaddedPCMQueue(std::shared_ptr<PCMQueue> downstream, int padSamples = PTT_PAD_SAMPLES)
        : downstream_(std::move(downstream)), padSamples_(padSamples) {}

    void Push(std::vector<int16_t> chunk) override {
        if (!leadInSent_.exchange(true))
            downstream_->Push(std::vector<int16_t>(padSamples_, 0));
        downstream_->Push(std::move(chunk));
    }

    void MarkDone() override {
        if (!doneSent_.exchange(true)) {
            if (leadInSent_.load())
                downstream_->Push(std::vector<int16_t>(padSamples_, 0));
            downstream_->MarkDone();
        }
        PCMQueue::MarkDone();
    }

private:
    std::shared_ptr<PCMQueue> downstream_;
    int padSamples_;
    std::atomic<bool> leadInSent_{false};
    std::atomic<bool> doneSent_{false};
};

} // namespace HoundTTS

#endif // HOUNDTTS_PCM_QUEUE_H
