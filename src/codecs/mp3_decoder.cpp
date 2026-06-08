#define MINIMP3_IMPLEMENTATION
#include "mp3_decoder.h"

#include <algorithm>

namespace HoundTTS {

// Resample from srcRate to 16000 Hz using linear interpolation.
static void ResampleAppend(const int16_t* src, size_t srcCount,
                           int srcRate, std::vector<int16_t>& out) {
    if (srcRate == 16000 || srcCount == 0) {
        out.insert(out.end(), src, src + srcCount);
        return;
    }

    double ratio = static_cast<double>(srcRate) / 16000.0;
    size_t dstCount = static_cast<size_t>(srcCount / ratio);
    if (dstCount == 0) return;

    size_t base = out.size();
    out.resize(base + dstCount);
    for (size_t i = 0; i < dstCount; ++i) {
        double pos = i * ratio;
        size_t idx = static_cast<size_t>(pos);
        double frac = pos - idx;

        if (idx + 1 < srcCount) {
            out[base + i] = static_cast<int16_t>(
                src[idx] * (1.0 - frac) + src[idx + 1] * frac);
        } else {
            out[base + i] = src[std::min(idx, srcCount - 1)];
        }
    }
}

Mp3Decoder::Mp3Decoder() {
    mp3dec_init(&m_dec);
}

std::vector<int16_t> Mp3Decoder::Decode(const uint8_t* data, size_t len) {
    // Append new data to any leftover from previous call
    m_buf.insert(m_buf.end(), data, data + len);

    std::vector<int16_t> out;
    mp3dec_frame_info_t info;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    size_t offset = 0;
    while (offset < m_buf.size()) {
        int samples = mp3dec_decode_frame(&m_dec,
            m_buf.data() + offset,
            static_cast<int>(m_buf.size() - offset),
            pcm, &info);

        if (info.frame_bytes == 0) break;  // incomplete frame at end
        offset += info.frame_bytes;

        if (samples <= 0) continue;

        // Mix to mono if stereo
        if (info.channels == 2) {
            for (int i = 0; i < samples; ++i) {
                pcm[i] = static_cast<int16_t>(
                    (static_cast<int32_t>(pcm[i * 2]) + pcm[i * 2 + 1]) / 2);
            }
        }

        // Resample to 16kHz if needed, append to output
        ResampleAppend(pcm, samples, info.hz, out);
    }

    // Keep unconsumed bytes for next call
    if (offset > 0) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + offset);
    }

    return out;
}

} // namespace HoundTTS
