#include "audio_queue.h"
#include <opus/opus.h>
#include "opus_encoder.h"
#include "utils.h"
#include <cstring>
#include <string>

namespace HoundTTS {

static void LogE(const std::string& msg) { Logger::Instance().Error("OpusFrameEncoder", msg); }

bool OpusFrameEncoder::Init() {
    // Destroy any encoder from a previous Init(): overwriting the raw pointer
    // would leak the libopus state. Single-threaded by contract — one encoder
    // instance is owned by its Opus worker thread.
    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }
    // Clear unconditionally: a failed re-init must not leave stale samples to
    // be emitted by a later successful Init().
    m_buf.clear();
    int err = 0;
    m_encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !m_encoder) return false;
    return true;
}

OpusFrameEncoder::~OpusFrameEncoder() {
    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }
}

void OpusFrameEncoder::EncodeFrame(const int16_t* frame, AudioQueue& queue) {
    // Init() not called/failed or bad frame pointer. The caller guarantees
    // kFrameSamples valid samples in `frame` (that is the only size this
    // encoder ever emits).
    if (!m_encoder || !frame) return;
    std::vector<uint8_t> packet(kMaxPacket);
    int len = opus_encode(m_encoder, frame, kFrameSamples,
                          packet.data(), kMaxPacket);
    if (len < 0) {
        // Codec error: the frame is lost, but surface it instead of dropping it silently.
        LogE("opus_encode failed: " + std::to_string(len));
        return;
    }
    if (len > 0) {
        packet.resize(len);
        queue.Push(std::move(packet));
    }
}

void OpusFrameEncoder::EncodeChunk(const int16_t* pcm, int samples, AudioQueue& queue) {
    // Don't accumulate audio when Init() was never called or failed: it would
    // never be emitted and the buffer would grow without bound.
    if (!m_encoder) return;
    if (!pcm || samples <= 0) return;
    // Sanity bound for a single chunk (64 s of 16 kHz audio). Callers pass
    // chunk.size(); this rejects a corrupted/oversized length before insert.
    static constexpr int kMaxChunkSamples = 1 << 20;
    if (samples > kMaxChunkSamples) {
        LogE("EncodeChunk rejected oversized chunk: " + std::to_string(samples) + " samples");
        return;
    }
    m_buf.insert(m_buf.end(), pcm, pcm + samples);

    while ((int)m_buf.size() >= kFrameSamples) {
        EncodeFrame(m_buf.data(), queue);
        m_buf.erase(m_buf.begin(), m_buf.begin() + kFrameSamples);
    }
}

void OpusFrameEncoder::Flush(AudioQueue& queue) {
    if (m_buf.empty()) return;
    m_buf.resize(kFrameSamples, 0);
    EncodeFrame(m_buf.data(), queue);
    m_buf.clear();
}

} // namespace HoundTTS
