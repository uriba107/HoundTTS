// PCM cache: request-key derivation, LRU/TTL eviction, byte accounting.
//
// A key regression is expensive: two different utterances collide, the second
// one plays the first one's audio, and the only symptom is "wrong message" in
// game. The eviction tests guard the memory ceiling.

#include "test_framework.h"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "backend.h"
#include "backends/pcm_cache.h"

using namespace HoundTTS;

namespace {

TTSRequest BaseRequest() {
    TTSRequest req;
    req.provider    = TtsProvider::Piper;
    req.message     = "hello";
    req.voice       = "en_US-amy-medium";
    req.gender      = "female";
    req.culture     = "en-US";
    req.speed       = 1.0;
    req.volume      = 1.0;
    req.awsPollyEngine = "neural";
    return req;
}

std::shared_ptr<std::vector<int16_t>> Pcm(std::initializer_list<int16_t> samples) {
    return std::make_shared<std::vector<int16_t>>(samples);
}

// Pin deterministic limits (the cache latches config on first use).
void ResetCache(size_t maxBytes = size_t(1024) * 1024,
                std::chrono::seconds ttl = std::chrono::seconds(0)) {
    PCMCache& cache = PCMCache::Instance();
    cache.Get(0);                  // latch ConfigReader once
    cache.SetEnabled(true);
    cache.SetMaxBytes(maxBytes);
    cache.SetTTL(ttl);
    cache.Clear();
}

} // namespace

void RunPcmCacheTests(testing::Context& ctx) {
    CASE(ctx, "ComputeTTSRequestKey is stable for identical requests") {
        CHECK(ctx, ComputeTTSRequestKey(BaseRequest()) == ComputeTTSRequestKey(BaseRequest()));
    }

    CASE(ctx, "ComputeTTSRequestKey changes with every audio-affecting field") {
        const uint64_t base = ComputeTTSRequestKey(BaseRequest());

        TTSRequest r = BaseRequest(); r.provider = TtsProvider::OpenAI;
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.message = "hello!";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.voice = "en_US-lessac-low";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.speaker = "2";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.gender = "male";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.culture = "en-GB";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.speed = 1.5;
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.volume = 0.5;
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.awsPollyEngine = "standard";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.isSSML = true;
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.translateProvider = TranslateProvider::OpenAI;
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.translateLanguage = "de";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);

        r = BaseRequest(); r.translateSourceLanguage = "fr";
        CHECK(ctx, ComputeTTSRequestKey(r) != base);
    }

    CASE(ctx, "ComputeTTSRequestKey ignores transmission-only fields") {
        const uint64_t base = ComputeTTSRequestKey(BaseRequest());

        TTSRequest r = BaseRequest();
        r.writedir  = "C:\\Saved Games\\DCS";
        r.transmitter = "discord";
        r.srsHost   = "10.0.0.5";
        r.srsPort   = 5003;
        r.freqs     = "251,252";
        r.modulations = "AM,FM";
        r.coalition = 2;
        r.name      = "Another Name";
        r.encrypt   = true;
        r.encKey    = 99;
        r.lat       = 45.0;
        r.lon       = 30.0;
        r.alt       = 1000.0;
        CHECK(ctx, ComputeTTSRequestKey(r) == base);
    }

    CASE(ctx, "ComputeTTSRequestKey quantises speed to 6 decimals") {
        TTSRequest a = BaseRequest(); a.speed = 1.0;
        TTSRequest b = BaseRequest(); b.speed = 1.0000001;
        CHECK(ctx, ComputeTTSRequestKey(a) == ComputeTTSRequestKey(b));
    }

    CASE(ctx, "ComputeTTSRequestKey: length prefixes prevent field-boundary aliasing") {
        // "ab" + "" must not hash the same as "a" + "b"
        TTSRequest a = BaseRequest(); a.message = "ab"; a.voice = "";
        TTSRequest b = BaseRequest(); b.message = "a";  b.voice = "b";
        CHECK(ctx, ComputeTTSRequestKey(a) != ComputeTTSRequestKey(b));
    }

    CASE(ctx, "PCMCache stores and returns the exact buffer") {
        ResetCache();
        PCMCache& cache = PCMCache::Instance();
        const uint64_t key = 1;

        CHECK(ctx, cache.Get(key) == nullptr);          // miss
        cache.Put(key, Pcm({1, 2, 3}));

        auto hit = cache.Get(key);
        CHECK(ctx, hit != nullptr);
        if (hit) {
            CHECK_EQ_INT(ctx, hit->size(), 3);
            CHECK_EQ_INT(ctx, (*hit)[1], 2);
        }
    }

    CASE(ctx, "PCMCache ignores empty buffers") {
        ResetCache();
        PCMCache& cache = PCMCache::Instance();
        cache.Put(7, std::make_shared<std::vector<int16_t>>());
        CHECK(ctx, cache.Get(7) == nullptr);
        CHECK_EQ_INT(ctx, cache.GetStats().entries, 0);
    }

    CASE(ctx, "PCMCache evicts least-recently-used entries at the byte cap") {
        // 2 samples = 4 bytes per entry; cap 8 bytes = 2 entries.
        ResetCache(8);
        PCMCache& cache = PCMCache::Instance();

        cache.Put(10, Pcm({1, 1}));
        cache.Put(11, Pcm({2, 2}));
        CHECK_EQ_INT(ctx, cache.GetStats().entries, 2);

        CHECK(ctx, cache.Get(10) != nullptr);   // promote 10, 11 becomes LRU

        cache.Put(12, Pcm({3, 3}));             // evicts 11
        CHECK(ctx, cache.Get(10) != nullptr);
        CHECK(ctx, cache.Get(12) != nullptr);
        CHECK(ctx, cache.Get(11) == nullptr);
        CHECK(ctx, cache.GetStats().evictions >= 1);
    }

    CASE(ctx, "PCMCache rejects an entry larger than the whole cache") {
        ResetCache(4);
        PCMCache& cache = PCMCache::Instance();
        cache.Put(20, Pcm({1, 2, 3}));          // 6 bytes > 4 byte cap
        CHECK(ctx, cache.Get(20) == nullptr);
        CHECK_EQ_INT(ctx, cache.GetStats().entries, 0);
    }

    CASE(ctx, "PCMCache replaces an existing key without double counting bytes") {
        ResetCache();
        PCMCache& cache = PCMCache::Instance();

        cache.Put(30, Pcm({1, 2}));
        cache.Put(30, Pcm({1, 2, 3, 4}));

        const PCMCache::Stats s = cache.GetStats();
        CHECK_EQ_INT(ctx, s.entries, 1);
        CHECK_EQ_INT(ctx, s.bytes, 4u * sizeof(int16_t));
        auto hit = cache.Get(30);
        CHECK(ctx, hit != nullptr);
        if (hit) CHECK_EQ_INT(ctx, hit->size(), 4);
    }

    CASE(ctx, "PCMCache honours the enabled flag") {
        ResetCache();
        PCMCache& cache = PCMCache::Instance();

        cache.SetEnabled(false);
        cache.Put(40, Pcm({1}));
        CHECK(ctx, cache.Get(40) == nullptr);

        cache.SetEnabled(true);
        CHECK(ctx, cache.Get(40) == nullptr);   // disabling dropped the entry
    }

    CASE(ctx, "PCMCache expires entries past the TTL") {
        ResetCache(size_t(1024) * 1024, std::chrono::seconds(1));
        PCMCache& cache = PCMCache::Instance();

        cache.Put(50, Pcm({1, 2}));
        CHECK(ctx, cache.Get(50) != nullptr);

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        CHECK(ctx, cache.Get(50) == nullptr);
        CHECK_EQ_INT(ctx, cache.GetStats().entries, 0);
    }

    CASE(ctx, "PCMCache Clear resets entries and counters") {
        ResetCache();
        PCMCache& cache = PCMCache::Instance();

        cache.Put(60, Pcm({1}));
        cache.Get(60);      // hit
        cache.Get(61);      // miss

        cache.Clear();
        const PCMCache::Stats s = cache.GetStats();
        CHECK_EQ_INT(ctx, s.entries, 0);
        CHECK_EQ_INT(ctx, s.bytes, 0);
        CHECK_EQ_INT(ctx, s.hits, 0);
        CHECK_EQ_INT(ctx, s.misses, 0);
        CHECK_EQ_INT(ctx, s.insertions, 0);
        CHECK_EQ_INT(ctx, s.evictions, 0);
    }
}
