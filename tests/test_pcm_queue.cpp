// PCM/audio queue contracts and the padding + caching wrappers.
//
// These queues are the hand-off between the TTS producer thread and the SRS
// consumer thread; a regression here manifests as stuck transmissions or audio
// that was cached but never played, which the DCS test mission cannot localise.

#include "test_framework.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "audio_queue.h"
#include "backends/pcm_cache.h"
#include "backends/pcm_queue.h"

using namespace HoundTTS;

namespace {

std::vector<int16_t> DrainAll(PCMQueue& q) {
    std::vector<int16_t> out;
    bool done = false;
    while (!done) {
        const std::vector<int16_t> chunk = q.Pop(2000, &done);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

bool AllZero(const std::vector<int16_t>& v, size_t from, size_t count) {
    if (from + count > v.size()) return false;
    for (size_t i = 0; i < count; ++i) {
        if (v[from + i] != 0) return false;
    }
    return true;
}

// PCMCache latches its limits from ConfigReader on first use, so let it latch
// now and then pin deterministic limits for the test.
void PrimeCache() {
    PCMCache& cache = PCMCache::Instance();
    cache.Get(0);
    cache.SetEnabled(true);
    cache.SetMaxBytes(size_t(1024) * 1024);
    cache.SetTTL(std::chrono::seconds(0));
    cache.Clear();
}

} // namespace

void RunPcmQueueTests(testing::Context& ctx) {
    CASE(ctx, "PCMQueue delivers chunks in order and reports completion") {
        PCMQueue q;
        q.Push({1, 2});
        q.Push({3});

        bool done = true;
        CHECK_EQ_INT(ctx, q.Pop(100, &done).size(), 2);
        CHECK(ctx, !done);
        const std::vector<int16_t> second = q.Pop(100, &done);
        CHECK_EQ_INT(ctx, second.size(), 1);
        CHECK_EQ_INT(ctx, second[0], 3);
        CHECK(ctx, !done);
        CHECK(ctx, !q.IsDone());

        q.MarkDone();
        CHECK(ctx, q.IsDone());
        CHECK_EQ_INT(ctx, q.Pop(10, &done).size(), 0);
        CHECK(ctx, done);
    }

    CASE(ctx, "PCMQueue::Pop times out without reporting done") {
        PCMQueue q;
        bool done = true;
        CHECK_EQ_INT(ctx, q.Pop(5, &done).size(), 0);
        CHECK(ctx, !done);
        CHECK(ctx, !q.IsDone());
    }

    CASE(ctx, "AudioQueue delivers frames and reports completion") {
        AudioQueue q;
        q.Push({0xAA, 0xBB});
        q.Push({0xCC});

        bool done = true;
        std::vector<uint8_t> f = q.Pop(100, &done);
        CHECK_EQ_INT(ctx, f.size(), 2);
        CHECK_EQ_INT(ctx, f[0], 0xAA);
        CHECK(ctx, !done);

        q.MarkDone();
        f = q.Pop(100, &done);
        CHECK_EQ_INT(ctx, f.size(), 1);
        CHECK(ctx, !done);
        CHECK_EQ_INT(ctx, q.Pop(10, &done).size(), 0);
        CHECK(ctx, done);
    }

    CASE(ctx, "PaddedPCMQueue wraps speech in lead-in and tail silence") {
        auto downstream = std::make_shared<PCMQueue>();
        PaddedPCMQueue padded(downstream, 100);

        padded.Push({7, 8, 9});
        padded.MarkDone();

        const std::vector<int16_t> all = DrainAll(*downstream);
        CHECK_EQ_INT(ctx, all.size(), 100u + 3u + 100u);
        CHECK(ctx, AllZero(all, 0, 100));
        CHECK_EQ_INT(ctx, all[100], 7);
        CHECK_EQ_INT(ctx, all[101], 8);
        CHECK_EQ_INT(ctx, all[102], 9);
        CHECK(ctx, AllZero(all, 103, 100));
    }

    CASE(ctx, "PaddedPCMQueue emits the lead-in only once across chunks") {
        auto downstream = std::make_shared<PCMQueue>();
        PaddedPCMQueue padded(downstream, 4);

        padded.Push({1});
        padded.Push({2});
        padded.MarkDone();

        const std::vector<int16_t> all = DrainAll(*downstream);
        CHECK_EQ_INT(ctx, all.size(), 4u + 2u + 4u);
        CHECK_EQ_INT(ctx, all[4], 1);
        CHECK_EQ_INT(ctx, all[5], 2);
    }

    CASE(ctx, "PaddedPCMQueue with no speech adds no padding at all") {
        auto downstream = std::make_shared<PCMQueue>();
        PaddedPCMQueue padded(downstream, 100);

        padded.MarkDone();

        CHECK_EQ_INT(ctx, DrainAll(*downstream).size(), 0);
        CHECK(ctx, downstream->IsDone());
    }

    CASE(ctx, "PaddedPCMQueue ignores pushes after completion") {
        auto downstream = std::make_shared<PCMQueue>();
        PaddedPCMQueue padded(downstream, 2);

        padded.Push({1});
        padded.MarkDone();
        padded.Push({2});          // late push from a straggling producer
        padded.MarkDone();         // idempotent

        const std::vector<int16_t> all = DrainAll(*downstream);
        CHECK_EQ_INT(ctx, all.size(), 2u + 1u + 2u);
        CHECK_EQ_INT(ctx, all[2], 1);
    }

    CASE(ctx, "CachingPCMQueue forwards to the downstream queue") {
        auto downstream = std::make_shared<PCMQueue>();
        CachingPCMQueue q(downstream, 0xDEADBEEF);
        q.Push({1, 2, 3});
        q.Push({4});
        q.MarkDone();

        const std::vector<int16_t> all = DrainAll(*downstream);
        CHECK_EQ_INT(ctx, all.size(), 4);
        CHECK_EQ_INT(ctx, all[0], 1);
        CHECK_EQ_INT(ctx, all[3], 4);
        CHECK(ctx, downstream->IsDone());
    }

    CASE(ctx, "CachingPCMQueue requires a downstream queue") {
        bool threw = false;
        try {
            CachingPCMQueue q(nullptr, 1);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(ctx, threw);
    }

    CASE(ctx, "CachingPCMQueue commits the accumulated PCM on success") {
        PrimeCache();
        auto downstream = std::make_shared<PCMQueue>();
        const uint64_t key = 0x1122334455667788ULL;

        {
            CachingPCMQueue q(downstream, key);
            q.Push({10, 20});
            q.Push({30});
            q.Finalize(true);
        }

        auto cached = PCMCache::Instance().Get(key);
        CHECK(ctx, cached != nullptr);
        if (cached) {
            CHECK_EQ_INT(ctx, cached->size(), 3);
            CHECK_EQ_INT(ctx, (*cached)[0], 10);
            CHECK_EQ_INT(ctx, (*cached)[2], 30);
        }
        // The downstream consumer still has the forwarded audio to drain before
        // it can observe completion.
        CHECK_EQ_INT(ctx, DrainAll(*downstream).size(), 3u);
        CHECK(ctx, downstream->IsDone());
    }

    CASE(ctx, "CachingPCMQueue does not commit after MarkFailed") {
        PrimeCache();
        auto downstream = std::make_shared<PCMQueue>();
        const uint64_t key = 0x1122334455667799ULL;

        {
            CachingPCMQueue q(downstream, key);
            q.Push({1, 2, 3});
            q.MarkDone();        // provider signalled completion...
            q.MarkFailed();      // ...but reported failure
            q.Finalize(false);
        }

        CHECK(ctx, PCMCache::Instance().Get(key) == nullptr);
        // Completion is still delivered downstream, or a consumer would hang.
        CHECK_EQ_INT(ctx, DrainAll(*downstream).size(), 3u);
        CHECK(ctx, downstream->IsDone());
    }

    CASE(ctx, "CachingPCMQueue drops pushes that arrive after Finalize") {
        PrimeCache();
        auto downstream = std::make_shared<PCMQueue>();
        const uint64_t key = 0x1122334455667800ULL;

        CachingPCMQueue q(downstream, key);
        q.Push({5});
        q.Finalize(true);
        q.Push({6});   // must not reach downstream or the cache

        auto cached = PCMCache::Instance().Get(key);
        CHECK(ctx, cached != nullptr);
        if (cached) CHECK_EQ_INT(ctx, cached->size(), 1);
        CHECK_EQ_INT(ctx, DrainAll(*downstream).size(), 1);
    }
}
