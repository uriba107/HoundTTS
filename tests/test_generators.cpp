// Tone and noise generators. These feed the jammer/test-tone paths that the
// DCS mission exercises by ear — the tests pin sample counts, determinism and
// the finite-input guards (NaN duration used to reach an int cast).

#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "backends/pcm_queue.h"
#include "providers/generators/noise.h"
#include "providers/generators/tone.h"
#include "session.h"

using namespace HoundTTS;

namespace {

std::vector<int16_t> DrainAll(PCMQueue& q) {
    std::vector<int16_t> out;
    bool done = false;
    while (!done) {
        const std::vector<int16_t> chunk = q.Pop(5000, &done);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

int PeakAbs(const std::vector<int16_t>& pcm) {
    int peak = 0;
    for (int16_t s : pcm) {
        const int v = (s < 0) ? -static_cast<int>(s) : static_cast<int>(s);
        if (v > peak) peak = v;
    }
    return peak;
}

bool AllZero(const std::vector<int16_t>& pcm) {
    for (int16_t s : pcm) {
        if (s != 0) return false;
    }
    return true;
}

std::vector<int16_t> RunTone(double durationS, float freqHz, float volume,
                             std::shared_ptr<Session> session = nullptr) {
    auto queue = std::make_shared<PCMQueue>();
    GenerateTone(queue, std::move(session), durationS, freqHz, volume);
    return DrainAll(*queue);
}

std::vector<int16_t> RunNoise(const std::string& type, uint32_t seed, float volume,
                              double duration,
                              std::shared_ptr<Session> session = nullptr) {
    auto queue = std::make_shared<PCMQueue>();
    GenerateNoise(queue, std::move(session), type, seed, volume, duration);
    return DrainAll(*queue);
}

} // namespace

void RunGeneratorTests(testing::Context& ctx) {
    CASE(ctx, "GenerateTone produces exactly ceil(duration*16k) samples") {
        const std::vector<int16_t> pcm = RunTone(0.1, 440.0f, 1.0f);
        CHECK_EQ_INT(ctx, pcm.size(), 1600);
        CHECK(ctx, PeakAbs(pcm) > 1000);

        const std::vector<int16_t> half = RunTone(0.05, 440.0f, 1.0f);
        CHECK_EQ_INT(ctx, half.size(), 800);
    }

    CASE(ctx, "GenerateTone is deterministic and volume 0 is silent") {
        const std::vector<int16_t> a = RunTone(0.05, 440.0f, 0.5f);
        const std::vector<int16_t> b = RunTone(0.05, 440.0f, 0.5f);
        CHECK(ctx, a == b);

        const std::vector<int16_t> silent = RunTone(0.05, 440.0f, 0.0f);
        CHECK_EQ_INT(ctx, silent.size(), 800);
        CHECK(ctx, AllZero(silent));
    }

    CASE(ctx, "GenerateTone clamps volume above 1.0") {
        CHECK_EQ_INT(ctx, RunTone(0.02, 440.0f, 5.0f).size(), 320);
        CHECK(ctx, PeakAbs(RunTone(0.02, 440.0f, 5.0f)) <= 32767);
    }

    CASE(ctx, "GenerateTone survives NaN/inf frequency and volume") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const float fnan = std::numeric_limits<float>::quiet_NaN();
        const float finf = std::numeric_limits<float>::infinity();

        const std::vector<int16_t> pcm = RunTone(0.02, fnan, finf);
        CHECK_EQ_INT(ctx, pcm.size(), 320);
        CHECK(ctx, PeakAbs(pcm) > 0);

        const std::vector<int16_t> pcm2 = RunTone(0.02, finf, fnan);
        CHECK_EQ_INT(ctx, pcm2.size(), 320);
        (void)nan;
    }

    CASE(ctx, "GenerateTone falls back to 2 s for a non-positive duration") {
        const std::vector<int16_t> pcm = RunTone(0.0, 440.0f, 1.0f);
        CHECK_EQ_INT(ctx, pcm.size(), 32000);
    }

    CASE(ctx, "GenerateTone stops immediately on a dead session and still completes") {
        auto session = std::make_shared<Session>();
        session->alive.store(false);

        auto queue = std::make_shared<PCMQueue>();
        GenerateTone(queue, session, 60.0, 440.0f, 1.0f);   // would be 960k samples

        CHECK_EQ_INT(ctx, DrainAll(*queue).size(), 0);
        CHECK(ctx, queue->IsDone());
    }

    CASE(ctx, "GenerateTone with a null queue does not crash") {
        GenerateTone(nullptr, nullptr, 0.1, 440.0f, 1.0f);
        CHECK(ctx, true);
    }

    CASE(ctx, "GenerateNoise is reproducible for a given seed") {
        const std::vector<int16_t> a = RunNoise("white", 1234, 1.0f, 0.1);
        const std::vector<int16_t> b = RunNoise("white", 1234, 1.0f, 0.1);
        const std::vector<int16_t> c = RunNoise("white", 9999, 1.0f, 0.1);

        CHECK_EQ_INT(ctx, a.size(), 1600);
        CHECK(ctx, a == b);
        CHECK(ctx, a != c);
        CHECK(ctx, PeakAbs(a) > 0);
    }

    CASE(ctx, "GenerateNoise handles every documented noise type") {
        const char* types[] = {"white", "pink", "chirp", "harsh", "jam", "unknown-type"};
        for (const char* type : types) {
            const std::vector<int16_t> pcm = RunNoise(type, 7, 1.0f, 0.1);
            CHECK_EQ_INT(ctx, pcm.size(), 1600);
            CHECK(ctx, PeakAbs(pcm) > 0);
        }
    }

    CASE(ctx, "GenerateNoise volume 0 is silent and volume is clamped") {
        CHECK(ctx, AllZero(RunNoise("jam", 1, 0.0f, 0.05)));
        CHECK(ctx, PeakAbs(RunNoise("jam", 1, 10.0f, 0.05)) <= 32767);
    }

    CASE(ctx, "GenerateNoise stops immediately on a dead session") {
        auto session = std::make_shared<Session>();
        session->alive.store(false);

        auto queue = std::make_shared<PCMQueue>();
        // duration <= 0 means "continuous" internally (capped at 2 h) — the dead
        // session is the only thing that keeps this test instant.
        GenerateNoise(queue, session, "jam", 1, 1.0f, 0.0);

        CHECK_EQ_INT(ctx, DrainAll(*queue).size(), 0);
        CHECK(ctx, queue->IsDone());
    }

    CASE(ctx, "GenerateNoise with a null queue does not crash") {
        GenerateNoise(nullptr, nullptr, "white", 1, 1.0f, 0.1);
        CHECK(ctx, true);
    }
}
