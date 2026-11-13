// Opus frame encoder — 40 ms framing, partial-frame buffering and flush.
//
// The encoder sits between PCM and the SRS UDP packets. Off-by-one framing is
// inaudible in a build check but shows up as audio drift or a truncated tail
// in-game, so the tests decode what was encoded.

#include "test_framework.h"

#include <opus/opus.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "audio_queue.h"
#include "codecs/opus_encoder.h"

using namespace HoundTTS;

namespace {

std::vector<uint8_t> PopFrame(AudioQueue& q) {
    bool done = false;
    return q.Pop(1000, &done);
}

} // namespace

void RunOpusEncoderTests(testing::Context& ctx) {
    CASE(ctx, "OpusFrameEncoder encodes full 40 ms frames") {
        OpusFrameEncoder encoder;
        CHECK(ctx, encoder.Init());

        AudioQueue queue;
        const std::vector<int16_t> pcm(OpusFrameEncoder::kFrameSamples * 2, 0);
        encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), queue);

        CHECK_EQ_INT(ctx, queue.Size(), 2);
        CHECK(ctx, !PopFrame(queue).empty());
        CHECK(ctx, !PopFrame(queue).empty());
    }

    CASE(ctx, "OpusFrameEncoder buffers partial frames until Flush") {
        OpusFrameEncoder encoder;
        CHECK(ctx, encoder.Init());

        AudioQueue queue;
        const std::vector<int16_t> partial(100, 0);
        encoder.EncodeChunk(partial.data(), static_cast<int>(partial.size()), queue);
        CHECK_EQ_INT(ctx, queue.Size(), 0);   // not enough samples for a frame

        encoder.Flush(queue);
        CHECK_EQ_INT(ctx, queue.Size(), 1);   // padded and emitted
    }

    CASE(ctx, "OpusFrameEncoder ignores null/empty input and empty flush") {
        OpusFrameEncoder encoder;
        CHECK(ctx, encoder.Init());

        AudioQueue queue;
        encoder.EncodeChunk(nullptr, 640, queue);
        encoder.EncodeChunk(nullptr, 0, queue);
        encoder.Flush(queue);                 // nothing buffered -> no frame
        CHECK_EQ_INT(ctx, queue.Size(), 0);
    }

    CASE(ctx, "Encoded frames round-trip through an Opus decoder") {
        OpusFrameEncoder encoder;
        CHECK(ctx, encoder.Init());

        AudioQueue queue;
        // A 440 Hz tone, so the decoded frame is not trivially silent.
        std::vector<int16_t> pcm(OpusFrameEncoder::kFrameSamples);
        for (size_t i = 0; i < pcm.size(); ++i) {
            const double t = static_cast<double>(i) / 16000.0;
            pcm[i] = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
        }
        encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), queue);
        // Pad to a frame so Flush emits something even if EncodeChunk did not.
        encoder.Flush(queue);

        int err = 0;
        OpusDecoder* decoder = opus_decoder_create(16000, 1, &err);
        CHECK(ctx, err == OPUS_OK && decoder != nullptr);
        if (decoder) {
            int decodedFrames = 0;
            while (queue.Size() > 0) {
                const std::vector<uint8_t> frame = PopFrame(queue);
                CHECK(ctx, !frame.empty());

                int16_t out[OpusFrameEncoder::kFrameSamples];
                const int samples = opus_decode(decoder, frame.data(),
                                                static_cast<opus_int32>(frame.size()),
                                                out, OpusFrameEncoder::kFrameSamples, 0);
                CHECK_EQ_INT(ctx, samples, OpusFrameEncoder::kFrameSamples);
                ++decodedFrames;
            }
            CHECK_EQ_INT(ctx, decodedFrames, 1);
            opus_decoder_destroy(decoder);
        }
    }

    CASE(ctx, "EncodeChunk without Init drops audio without crashing") {
        OpusFrameEncoder encoder;   // Init() deliberately not called
        AudioQueue queue;
        const std::vector<int16_t> pcm(OpusFrameEncoder::kFrameSamples, 0);
        encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), queue);
        CHECK_EQ_INT(ctx, queue.Size(), 0);
    }
}
