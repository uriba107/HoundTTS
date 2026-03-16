#pragma once

#ifndef HOUNDTTS_AUDIO_QUEUE_H
#define HOUNDTTS_AUDIO_QUEUE_H

#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>

namespace HoundTTS {

// Thread-safe queue of Opus-encoded audio frames.
// Producer calls Push() then MarkDone() when finished.
// Consumer calls Pop() which blocks until a frame is available or done.
class AudioQueue {
public:
    AudioQueue() : m_done(false) {}

    void Push(std::vector<uint8_t> frame) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(std::move(frame));
        }
        m_cv.notify_one();
    }

    // Returns frame, or empty vector if timed out.
    // Sets *done=true when producer has finished AND queue is empty.
    std::vector<uint8_t> Pop(int timeoutMs, bool* done) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !m_queue.empty() || m_done; });
        }
        if (!m_queue.empty()) {
            auto frame = std::move(m_queue.front());
            m_queue.pop_front();
            *done = false;
            return frame;
        }
        *done = m_done;
        return {};
    }

    void MarkDone() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    bool IsDone() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_done && m_queue.empty();
    }

private:
    std::deque<std::vector<uint8_t>> m_queue;
    std::mutex                       m_mutex;
    std::condition_variable          m_cv;
    bool                             m_done;
};

} // namespace HoundTTS

#endif // HOUNDTTS_AUDIO_QUEUE_H
