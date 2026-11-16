#include "audio_queue.h"
#include <opus/opus.h>
#include "opus_encoder.h"
#include "utils.h"
#include <cstring>
#include <string>

namespace HoundTTS {

static void LogE(const std::string& msg) { Logger::Instance().Error("OpusFrameEncoder", msg); }

bool OpusFrameEncoder::Init() {
    // Create the new encoder FIRST and swap only on success: a failed re-init
    // must leave a working encoder (and its buffered audio) untouched.
    int err = 0;
    OpusEncoder* created = opus_encoder_create(kSampleRate, kChannels,
                                                OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !created) {
        if (created) opus_encoder_destroy(created);
        return false;
    }
    if (m_encoder) opus_encoder_destroy(m_encoder);
    m_encoder = created;
    // Clear: a successful re-init must not emit stale samples from before.
    m_buf.clear();
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
    if (len > kMaxPacket) {
        // Defensive: libopus never exceeds the requested capacity, but an
        // oversized value must not resize the packet past the encoded region.
        LogE("opus_encode returned oversized packet: " + std::to_string(len));
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
    // No encoder means nothing can be produced; drop the buffer instead of
    // pretending to encode it. Drain any complete frames first: a violated
    // invariant (buffered >= one frame) must not truncate the tail.
    if (!m_encoder) {
        m_buf.clear();
        return;
    }
    while (static_cast<int>(m_buf.size()) >= kFrameSamples) {
        EncodeFrame(m_buf.data(), queue);
        m_buf.erase(m_buf.begin(), m_buf.begin() + kFrameSamples);
    }
    if (m_buf.empty()) return;
    m_buf.resize(kFrameSamples, 0);  // pad the tail to one full Opus frame
    EncodeFrame(m_buf.data(), queue);
    m_buf.clear();
}

} // namespace HoundTTS
