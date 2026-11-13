// Piper voice registry — the folder scan that decides whether a requested
// voice exists and whether the fallback is available. A regression here is
// silent: the pipeline falls back to the default voice and the mission just
// "sounds wrong".

#include "test_framework.h"
#include "test_tmpdir.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "providers/tts/piper/piper_voice_registry.h"

using namespace HoundTTS;

void RunVoiceRegistryTests(testing::Context& ctx) {
    CASE(ctx, "PiperVoiceRegistry scans the voices folder case-insensitively") {
        testing::TempDir dir("piper-voices");
        const std::filesystem::path voices = dir.Path();

        // Two real (.onnx, mixed case) voices plus files that must be ignored.
        std::ofstream(voices / "en_US-amy-medium.onnx") << "x";
        std::ofstream(voices / "DE_THORSTEN.ONNX") << "x";
        std::ofstream(voices / "readme.txt") << "x";
        std::ofstream(voices / "en_US-noext") << "x";

        PiperVoiceRegistry& registry = PiperVoiceRegistry::Instance();

        // Before Initialize() nothing is known.
        CHECK(ctx, !registry.IsVoiceAvailable("en_US-amy-medium"));
        CHECK(ctx, !registry.IsDefaultVoiceAvailable());

        registry.Initialize(dir.Utf8(), "en_US-amy-medium");

        CHECK(ctx, registry.IsVoiceAvailable("en_US-amy-medium"));
        CHECK(ctx, registry.IsVoiceAvailable("EN_US-AMY-MEDIUM"));
        CHECK(ctx, registry.IsVoiceAvailable("de_thorsten"));   // uppercase .ONNX on disk
        CHECK(ctx, !registry.IsVoiceAvailable("en_US-lessac-low"));
        CHECK(ctx, !registry.IsVoiceAvailable("readme"));
        // The registry stores basenames without the extension; the pipeline
        // strips ".onnx" before lookup, so a full filename must not match.
        CHECK(ctx, !registry.IsVoiceAvailable("en_US-amy-medium.onnx"));
        CHECK(ctx, registry.IsDefaultVoiceAvailable());
        CHECK_EQ_INT(ctx, registry.GetAvailableVoices().size(), 2);
        CHECK_STR_EQ(ctx, registry.GetVoiceFolderPath(), dir.Utf8());
    }

    CASE(ctx, "PiperVoiceRegistry keeps reporting unknown voices as unavailable") {
        // Initialize() is once-per-process; this pins the post-init negative
        // path the pipeline relies on to fall back instead of opening a model
        // file that does not exist.
        PiperVoiceRegistry& registry = PiperVoiceRegistry::Instance();
        CHECK(ctx, !registry.IsVoiceAvailable("anything"));
        CHECK(ctx, !registry.IsVoiceAvailable(""));
    }
}
