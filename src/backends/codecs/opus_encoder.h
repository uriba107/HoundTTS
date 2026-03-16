#pragma once

#ifndef HOUNDTTS_OPUS_ENCODER_H
#define HOUNDTTS_OPUS_ENCODER_H

#include "../audio_queue.h"
#include <vector>
#include <cstdint>

// Forward-declare libopus type to avoid pulling opus.h into every TU
struct OpusEncoder;

namespace HoundTTS {

// Wraps libopus to encode 16kHz mono 16-bit PCM into 40ms Opus frames.
// Frames are pushed to an AudioQueue as they are produced.
// Handles partial frames across EncodeChunk() calls via an internal buffer.
class OpusFrameEncoder {
public:
    static constexpr int kSampleRate   = 16000;
    static constexpr int kChannels     = 1;
    static constexpr int kFrameSamples = 640;   // 40ms @ 16kHz
    static constexpr int kMaxPacket    = 4000;  // bytes, generous upper bound

    // Returns true on success. Call before any EncodeChunk/Flush.
    bool Init();

    // Encode a chunk of PCM samples, pushing complete 40ms frames to queue.
    // May be called multiple times; partial frames are buffered internally.
    void EncodeChunk(const int16_t* pcm, int samples, AudioQueue& queue);

    // Pad the internal buffer to a full frame and push the final frame.
    // Must be called once after all EncodeChunk calls.
    void Flush(AudioQueue& queue);

    ~OpusFrameEncoder();

private:
    OpusEncoder* m_encoder = nullptr;
    std::vector<int16_t> m_buf;   // partial-frame accumulator

    void EncodeFrame(const int16_t* frame, AudioQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_OPUS_ENCODER_H
