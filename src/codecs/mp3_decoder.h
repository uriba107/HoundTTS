#pragma once

#ifndef HOUNDTTS_MP3_DECODER_H
#define HOUNDTTS_MP3_DECODER_H

#include "minimp3.h"

#include <cstdint>
#include <vector>

namespace HoundTTS {

// Streaming MP3→16kHz-mono-PCM decoder.
// Holds minimp3 state across calls so MDCT overlap is preserved.
// Create once per synthesis, call Decode() for each chunk of MP3 data.
class Mp3Decoder {
public:
    Mp3Decoder();

    // Feed MP3 bytes, get back 16kHz 16-bit mono PCM.
    // Partial MP3 frames are buffered internally for the next call.
    std::vector<int16_t> Decode(const uint8_t* data, size_t len);

private:
    mp3dec_t m_dec;
    std::vector<uint8_t> m_buf;   // leftover bytes from previous call
};

} // namespace HoundTTS

#endif // HOUNDTTS_MP3_DECODER_H
