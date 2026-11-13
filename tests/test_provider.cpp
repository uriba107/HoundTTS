// Provider name parsing / formatting — the single source of truth for every
// Lua alias. A regression here silently misroutes a mission's audio.

#include "test_framework.h"

#include "provider.h"

using namespace HoundTTS;

void RunProviderTests(testing::Context& ctx) {
    CASE(ctx, "NormalizeProviderToken trims and lowercases") {
        CHECK_STR_EQ(ctx, NormalizeProviderToken("  Piper \t"), "piper");
        CHECK_STR_EQ(ctx, NormalizeProviderToken("SAPI"), "sapi");
        CHECK_STR_EQ(ctx, NormalizeProviderToken(""), "");
        CHECK_STR_EQ(ctx, NormalizeProviderToken("   "), "");
    }

    CASE(ctx, "ParseTtsProvider honours every documented alias") {
        CHECK(ctx, ParseTtsProvider("sapi")       == TtsProvider::Sapi);
        CHECK(ctx, ParseTtsProvider("win")        == TtsProvider::Sapi);
        CHECK(ctx, ParseTtsProvider("")           == TtsProvider::Sapi);   // default
        CHECK(ctx, ParseTtsProvider("piper")      == TtsProvider::Piper);
        CHECK(ctx, ParseTtsProvider("azure")      == TtsProvider::Azure);
        CHECK(ctx, ParseTtsProvider("google")     == TtsProvider::Google);
        CHECK(ctx, ParseTtsProvider("gcloud")     == TtsProvider::Google);
        CHECK(ctx, ParseTtsProvider("elevenlabs") == TtsProvider::ElevenLabs);
        CHECK(ctx, ParseTtsProvider("aws")        == TtsProvider::AWS);
        CHECK(ctx, ParseTtsProvider("polly")      == TtsProvider::AWS);
        CHECK(ctx, ParseTtsProvider("amazon")     == TtsProvider::AWS);
        CHECK(ctx, ParseTtsProvider("openai")     == TtsProvider::OpenAI);
        CHECK(ctx, ParseTtsProvider("supertonic") == TtsProvider::Supertonic);
        CHECK(ctx, ParseTtsProvider("edge")       == TtsProvider::Edge);
        CHECK(ctx, ParseTtsProvider("edgetts")    == TtsProvider::Edge);
    }

    CASE(ctx, "ParseTtsProvider is case/whitespace tolerant and rejects junk") {
        CHECK(ctx, ParseTtsProvider("  EDGE  ")  == TtsProvider::Edge);
        CHECK(ctx, ParseTtsProvider("Piper")     == TtsProvider::Piper);
        CHECK(ctx, ParseTtsProvider("edge-tts")  == TtsProvider::Unknown);
        CHECK(ctx, ParseTtsProvider("kitten")    == TtsProvider::Unknown);
    }

    CASE(ctx, "TtsProviderName round-trips every enum value") {
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Sapi),       "sapi");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Piper),      "piper");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Azure),      "azure");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Google),     "google");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::ElevenLabs), "elevenlabs");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::AWS),        "aws");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::OpenAI),     "openai");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Supertonic), "supertonic");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Edge),       "edge");
        CHECK_STR_EQ(ctx, TtsProviderName(TtsProvider::Unknown),    "unknown");

        // Every name we emit must parse back to the same enum (cache-key safety:
        // ComputeTTSRequestKey folds TtsProviderName into the hash).
        const TtsProvider all[] = {
            TtsProvider::Sapi, TtsProvider::Piper, TtsProvider::Azure,
            TtsProvider::Google, TtsProvider::ElevenLabs, TtsProvider::AWS,
            TtsProvider::OpenAI, TtsProvider::Supertonic, TtsProvider::Edge,
        };
        for (TtsProvider p : all) {
            CHECK(ctx, ParseTtsProvider(TtsProviderName(p)) == p);
        }
    }

    CASE(ctx, "ParseTranslateProvider honours aliases and None default") {
        CHECK(ctx, ParseTranslateProvider("")              == TranslateProvider::None);
        CHECK(ctx, ParseTranslateProvider("  ")            == TranslateProvider::None);
        CHECK(ctx, ParseTranslateProvider("openai")        == TranslateProvider::OpenAI);
        CHECK(ctx, ParseTranslateProvider("google")        == TranslateProvider::Google);
        CHECK(ctx, ParseTranslateProvider("gcloud")        == TranslateProvider::Google);
        CHECK(ctx, ParseTranslateProvider("libretranslate") == TranslateProvider::LibreTranslate);
        CHECK(ctx, ParseTranslateProvider("libre")         == TranslateProvider::LibreTranslate);
        CHECK(ctx, ParseTranslateProvider("aws")           == TranslateProvider::AWS);
        CHECK(ctx, ParseTranslateProvider("amazon")        == TranslateProvider::AWS);
        CHECK(ctx, ParseTranslateProvider("azure")         == TranslateProvider::Azure);
        CHECK(ctx, ParseTranslateProvider("deepl")         == TranslateProvider::Unknown);
    }

    CASE(ctx, "TranslateProviderName: None is empty, unknown is 'unknown'") {
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::None), "");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::OpenAI), "openai");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::Google), "google");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::LibreTranslate), "libretranslate");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::AWS), "aws");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::Azure), "azure");
        CHECK_STR_EQ(ctx, TranslateProviderName(TranslateProvider::Unknown), "unknown");
    }
}
