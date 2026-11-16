// Speech-duration estimation (speech_time.h). Lua callers use this for F10
// menu timing and subtitle sync, so the numbers are a public contract.

#include "test_framework.h"

#include <cmath>
#include <limits>

#include "speech_time.h"

using namespace HoundTTS;

void RunSpeechTimeTests(testing::Context& ctx) {
    CASE(ctx, "GetSpeechTime returns 0 for non-positive length") {
        CHECK_NEAR(ctx, GetSpeechTime(0, 1.0, TtsProvider::Piper), 0.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(-10, 1.0, TtsProvider::Sapi), 0.0, 1e-9);
    }

    CASE(ctx, "GetSpeechTime: 80 chars at normal rate is 10 s for every provider") {
        // 100 wpm at speed factor 1.0 -> 500/60 = 8 cps -> ceil(80/8) = 10
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::Piper), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::Google), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::AWS), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::OpenAI), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::Azure), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::Edge), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, 1.0, TtsProvider::Supertonic), 10.0, 1e-9);
    }

    CASE(ctx, "SAPI rate scale: 0 is normal, +5 is ~2x, -5 is ~half") {
        // SAPI 0 -> factor 1.0
        CHECK_NEAR(ctx, GetSpeechTime(80, 0.0, TtsProvider::Sapi), 10.0, 1e-9);
        // +5 -> 5*2/10+1 = 2.0 -> 16 cps -> 5 s
        CHECK_NEAR(ctx, GetSpeechTime(80, 5.0, TtsProvider::Sapi), 5.0, 1e-9);
        // -5 -> factor 0.5 -> 50 wpm -> 4 cps -> 20 s
        CHECK_NEAR(ctx, GetSpeechTime(80, -5.0, TtsProvider::Sapi), 20.0, 1e-9);
    }

    CASE(ctx, "Non-finite speed falls back to the provider default") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        CHECK_NEAR(ctx, GetSpeechTime(80, nan, TtsProvider::Piper), 10.0, 1e-9);
        CHECK_NEAR(ctx, GetSpeechTime(80, inf, TtsProvider::Piper), 10.0, 1e-9);
        // Sapi fallback is rate 0 (= factor 1.0), not 1.0 rate (= factor 1.2)
        CHECK_NEAR(ctx, GetSpeechTime(80, nan, TtsProvider::Sapi), 10.0, 1e-9);
    }

    CASE(ctx, "Per-provider speed clamps are applied") {
        // ElevenLabs clamps to 1.2 -> 120 wpm -> 10 cps -> 8 s
        CHECK_NEAR(ctx, GetSpeechTime(80, 5.0, TtsProvider::ElevenLabs), 8.0, 1e-9);
        // Piper clamps to 10.0 -> 1000 wpm -> 83 cps -> ceil(80/83) = 1 s
        CHECK_NEAR(ctx, GetSpeechTime(80, 100.0, TtsProvider::Piper), 1.0, 1e-9);
        // Supertonic clamps to 0.5 -> 50 wpm -> 4 cps -> 20 s
        CHECK_NEAR(ctx, GetSpeechTime(80, 0.01, TtsProvider::Supertonic), 20.0, 1e-9);
        // Azure/Edge clamp low end to 0.5 as well
        CHECK_NEAR(ctx, GetSpeechTime(80, 0.01, TtsProvider::Edge), 20.0, 1e-9);
    }

    CASE(ctx, "Faster speed never estimates longer than slower speed") {
        for (double slow = 0.5; slow < 2.0; slow += 0.25) {
            const double a = GetSpeechTime(500, slow, TtsProvider::Piper);
            const double b = GetSpeechTime(500, slow + 0.25, TtsProvider::Piper);
            CHECK(ctx, b <= a);
        }
    }

    CASE(ctx, "Estimate is never zero for a non-empty message") {
        CHECK(ctx, GetSpeechTime(1, 10.0, TtsProvider::Piper) >= 1.0);
    }
}
