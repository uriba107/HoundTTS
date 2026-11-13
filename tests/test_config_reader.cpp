// ConfigReader INI parsing. This is the only place mission-config typos turn
// into silent behaviour changes (wrong voice, wrong endpoint, cache off), so the
// quoting/comment/clamping rules are pinned here.

#include "test_framework.h"
#include "test_tmpdir.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "config_reader.h"

using namespace HoundTTS;

namespace {

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f << content;
}

// ConfigReader looks for <writedir>\Config\HoundTTS-credentials.ini
void WriteIni(const std::filesystem::path& writedir, const std::string& content) {
    WriteFile(writedir / "Config" / "HoundTTS-credentials.ini", content);
}

} // namespace

void RunConfigReaderTests(testing::Context& ctx) {
    CASE(ctx, "ConfigReader parses every section, comment style and quote style") {
        testing::TempDir dir("config-full");
        WriteIni(dir.Path(),
            "; leading comment\n"
            "# another comment\n"
            "\n"
            "[Piper]\n"
            "path = C:\\TTS\\piper.exe\n"
            "voice_path = D:\\voices\n"
            "max_concurrent = 6\n"
            "\n"
            "[Google]\n"
            "credentials_file = \"C:\\keys\\svc account.json\"\n"
            "\n"
            "[Azure]\n"
            "key = az-key ; trailing comment\n"
            "region = westeurope\n"
            "\n"
            "[ElevenLabs]\n"
            "api_key = el-key\n"
            "model_id = eleven_turbo_v2_5\n"
            "\n"
            "[OpenAI]\n"
            "api_key = \"sk-a;b\"\n"
            "endpoint = https://example.com/v1#frag\n"
            "model = tts-1-hd\n"
            "chat_model = gpt-4o\n"
            "\n"
            "[AWS]\n"
            "access_key = AKID\n"
            "secret_key = SECRET\n"
            "region = eu-west-1\n"
            "engine = long-form\n"
            "\n"
            "[LibreTranslate]\n"
            "endpoint = http://127.0.0.1:5000\n"
            "api_key = libre-key\n"
            "\n"
            "[Supertonic]\n"
            "lang = de\n"
            "total_steps = 12\n"
            "speed = 1.5\n"
            "threads = 2\n"
            "max_concurrent = 3\n"
            "voice_style = F2\n"
            "\n"
            "[BogusSection]\n"
            "key = ignored\n"
            "\n"
            "[Edge]\n"
            "buffer_ms = 5000\n"
            "\n"
            "[Discord]\n"
            "bot_token = tok\n"
            "\n"
            "[General]\n"
            "log_level = info\n"
            "cache_enabled = no\n"
            "cache_max_mb = 42\n"
            "cache_ttl_minutes = 0\n");

        ConfigReader& cfg = ConfigReader::Instance();
        cfg.Load(dir.Utf8());

        // Trailing "piper.exe" is stripped to leave the containing directory.
        CHECK_STR_EQ(ctx, cfg.GetPiperPath(), "C:\\TTS");
        CHECK_STR_EQ(ctx, cfg.GetPiperVoicePath(), "D:\\voices");
        CHECK_EQ_INT(ctx, cfg.GetPiperMaxConcurrent(), 6);

        CHECK_STR_EQ(ctx, cfg.GetGoogleCredsFile(), "C:\\keys\\svc account.json");

        // ';' starts a comment only outside a quoted value.
        CHECK_STR_EQ(ctx, cfg.GetAzureKey(), "az-key");
        CHECK_STR_EQ(ctx, cfg.GetAzureRegion(), "westeurope");

        CHECK_STR_EQ(ctx, cfg.GetElevenLabsKey(), "el-key");
        CHECK_STR_EQ(ctx, cfg.GetElevenLabsModelId(), "eleven_turbo_v2_5");

        CHECK_STR_EQ(ctx, cfg.GetOpenAIKey(), "sk-a;b");           // quoted ';' kept
        CHECK_STR_EQ(ctx, cfg.GetOpenAIEndpoint(), "https://example.com/v1#frag");
        CHECK_STR_EQ(ctx, cfg.GetOpenAIModel(), "tts-1-hd");
        CHECK_STR_EQ(ctx, cfg.GetOpenAIChatModel(), "gpt-4o");

        CHECK_STR_EQ(ctx, cfg.GetAwsAccessKey(), "AKID");
        CHECK_STR_EQ(ctx, cfg.GetAwsSecretKey(), "SECRET");
        CHECK_STR_EQ(ctx, cfg.GetAwsRegion(), "eu-west-1");
        CHECK_STR_EQ(ctx, cfg.GetAwsPollyEngine(), "long-form");

        CHECK_STR_EQ(ctx, cfg.GetLibreTranslateEndpoint(), "http://127.0.0.1:5000");
        CHECK_STR_EQ(ctx, cfg.GetLibreTranslateApiKey(), "libre-key");

        CHECK_STR_EQ(ctx, cfg.GetSupertonicLang(), "de");
        CHECK_EQ_INT(ctx, cfg.GetSupertonicTotalSteps(), 12);
        CHECK_NEAR(ctx, cfg.GetSupertonicSpeed(), 1.5, 1e-6);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicThreads(), 2);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicMaxConcurrent(), 3);
        CHECK_STR_EQ(ctx, cfg.GetSupertonicVoiceStyle(), "F2");

        CHECK_EQ_INT(ctx, cfg.GetEdgeBufferMs(), 5000);
        CHECK_STR_EQ(ctx, cfg.GetDiscordToken(), "tok");

        CHECK_STR_EQ(ctx, cfg.GetLogLevel(), "info");
        CHECK(ctx, !cfg.GetCacheEnabled());
        CHECK_EQ_INT(ctx, cfg.GetCacheMaxMb(), 42);
        CHECK_EQ_INT(ctx, cfg.GetCacheTtlMinutes(), 0);  // 0 = no TTL, allowed

        CHECK_STR_EQ(ctx, cfg.GetWritedir(), dir.Utf8());
    }

    CASE(ctx, "ConfigReader applies bundled defaults when the INI is missing") {
        testing::TempDir dir("config-defaults");
        ConfigReader& cfg = ConfigReader::Instance();
        cfg.Load(dir.Utf8());

        const std::string root = dir.Utf8() + "\\";
        CHECK_STR_EQ(ctx, cfg.GetPiperPath(),      root + "Mods\\Services\\HoundTTS\\bin\\piper");
        CHECK_STR_EQ(ctx, cfg.GetPiperVoicePath(), root + "Mods\\Services\\HoundTTS\\voices\\");
        CHECK_STR_EQ(ctx, cfg.GetSupertonicPath(),      root + "Mods\\Services\\HoundTTS\\bin\\supertonic");
        CHECK_STR_EQ(ctx, cfg.GetSupertonicModelPath(), root + "Mods\\Services\\HoundTTS\\bin\\supertonic\\models");
        CHECK_STR_EQ(ctx, cfg.GetSupertonicVoiceStylePath(),
                     root + "Mods\\Services\\HoundTTS\\bin\\supertonic\\voice_styles");
        CHECK_STR_EQ(ctx, cfg.GetSupertonicVoiceStyle(), "M1");
        CHECK_STR_EQ(ctx, cfg.GetSupertonicLang(), "en");

        CHECK_STR_EQ(ctx, cfg.GetElevenLabsModelId(), "eleven_turbo_v2");
        CHECK_STR_EQ(ctx, cfg.GetAwsPollyEngine(), "neural");
        CHECK_STR_EQ(ctx, cfg.GetOpenAIEndpoint(), "https://api.openai.com");
        CHECK_STR_EQ(ctx, cfg.GetOpenAIModel(), "tts-1");
        CHECK_STR_EQ(ctx, cfg.GetOpenAIChatModel(), "gpt-4o-mini");
        CHECK_STR_EQ(ctx, cfg.GetLibreTranslateEndpoint(), "http://localhost:5000");

        CHECK_EQ_INT(ctx, cfg.GetPiperMaxConcurrent(), 4);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicTotalSteps(), 8);
        CHECK_NEAR(ctx, cfg.GetSupertonicSpeed(), 1.05, 1e-6);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicThreads(), 4);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicMaxConcurrent(), 2);
        CHECK_EQ_INT(ctx, cfg.GetEdgeBufferMs(), 200);

        CHECK_STR_EQ(ctx, cfg.GetLogLevel(), "error");
        CHECK(ctx, cfg.GetCacheEnabled());
        CHECK_EQ_INT(ctx, cfg.GetCacheMaxMb(), 100);
        CHECK_EQ_INT(ctx, cfg.GetCacheTtlMinutes(), 5);

        // Empty credentials stay empty — never invented from a default.
        CHECK_STR_EQ(ctx, cfg.GetAzureKey(), "");
        CHECK_STR_EQ(ctx, cfg.GetAwsSecretKey(), "");
        CHECK_STR_EQ(ctx, cfg.GetDiscordToken(), "");
    }

    CASE(ctx, "ConfigReader ignores out-of-range and unparseable numbers") {
        testing::TempDir dir("config-clamps");
        WriteIni(dir.Path(),
            "[Piper]\n"
            "max_concurrent = 0\n"
            "[Edge]\n"
            "buffer_ms = 9999\n"
            "[Supertonic]\n"
            "total_steps = -3\n"
            "speed = 0\n"
            "threads = abc\n"
            "[General]\n"
            "cache_max_mb = -5\n"
            "cache_ttl_minutes = -1\n"
            "cache_enabled = false\n");

        ConfigReader& cfg = ConfigReader::Instance();
        cfg.Load(dir.Utf8());

        CHECK_EQ_INT(ctx, cfg.GetPiperMaxConcurrent(), 4);
        CHECK_EQ_INT(ctx, cfg.GetEdgeBufferMs(), 200);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicTotalSteps(), 8);
        CHECK_NEAR(ctx, cfg.GetSupertonicSpeed(), 1.05, 1e-6);
        CHECK_EQ_INT(ctx, cfg.GetSupertonicThreads(), 4);
        CHECK_EQ_INT(ctx, cfg.GetCacheMaxMb(), 100);
        CHECK_EQ_INT(ctx, cfg.GetCacheTtlMinutes(), 5);
        CHECK(ctx, !cfg.GetCacheEnabled());
    }

    CASE(ctx, "ConfigReader reloads for a new writedir and drops stale values") {
        testing::TempDir first("config-reload-a");
        WriteIni(first.Path(), "[General]\nlog_level = debug\n[Piper]\npath = C:\\stale\n");

        ConfigReader& cfg = ConfigReader::Instance();
        cfg.Load(first.Utf8());
        CHECK_STR_EQ(ctx, cfg.GetLogLevel(), "debug");
        CHECK_STR_EQ(ctx, cfg.GetPiperPath(), "C:\\stale");

        testing::TempDir second("config-reload-b");
        cfg.Load(second.Utf8());

        // New writedir: values must not leak across, defaults recomputed.
        CHECK_STR_EQ(ctx, cfg.GetLogLevel(), "error");
        CHECK_STR_EQ(ctx, cfg.GetPiperPath(),
                     second.Utf8() + "\\Mods\\Services\\HoundTTS\\bin\\piper");
        CHECK_STR_EQ(ctx, cfg.GetWritedir(), second.Utf8());

        // Same writedir twice is a no-op (idempotent init on mission reload).
        cfg.Load(second.Utf8());
        CHECK_STR_EQ(ctx, cfg.GetWritedir(), second.Utf8());
    }
}
