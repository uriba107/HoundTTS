#include "audio_queue.h"
#include <opus/opus.h>
#include "opus_encoder.h"
#include <cstring>

namespace HoundTTS {

bool OpusFrameEncoder::Init() {
    int err = 0;
    m_encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !m_encoder) return false;
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
    std::vector<uint8_t> packet(kMaxPacket);
    int len = opus_encode(m_encoder, frame, kFrameSamples,
                          packet.data(), kMaxPacket);
    if (len > 0) {
        packet.resize(len);
        queue.Push(std::move(packet));
    }
}

void OpusFrameEncoder::EncodeChunk(const int16_t* pcm, int samples, AudioQueue& queue) {
    // Append incoming samples to buffer
    m_buf.insert(m_buf.end(), pcm, pcm + samples);

    // Drain complete 40ms frames
    while ((int)m_buf.size() >= kFrameSamples) {
        EncodeFrame(m_buf.data(), queue);
        m_buf.erase(m_buf.begin(), m_buf.begin() + kFrameSamples);
    }
}

void OpusFrameEncoder::Flush(AudioQueue& queue) {
    if (m_buf.empty()) return;
    // Pad to full frame with silence
    m_buf.resize(kFrameSamples, 0);
    EncodeFrame(m_buf.data(), queue);
    m_buf.clear();
}

} // namespace HoundTTS
