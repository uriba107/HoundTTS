// Entry point for the host-side unit tests. Each Run*Tests() file registers its
// own cases against the shared Context; adding a unit means adding one line here
// and one file next to this one.

#include "test_framework.h"

void RunProviderTests(testing::Context& ctx);
void RunSpeechTimeTests(testing::Context& ctx);
void RunSrsTypesTests(testing::Context& ctx);
void RunUtilsTests(testing::Context& ctx);
void RunConfigReaderTests(testing::Context& ctx);
void RunPcmQueueTests(testing::Context& ctx);
void RunPcmCacheTests(testing::Context& ctx);
void RunGeneratorTests(testing::Context& ctx);
void RunSrsPacketTests(testing::Context& ctx);
void RunAwsAuthTests(testing::Context& ctx);
void RunEdgeDrmTests(testing::Context& ctx);
void RunVoiceRegistryTests(testing::Context& ctx);
void RunOpusEncoderTests(testing::Context& ctx);

int main() {
    testing::Context ctx;

    // One throwing suite must not abort the rest of the run (or escape main and
    // skip the summary): each suite is isolated and recorded as a failure.
    const struct {
        const char* name;
        void (*run)(testing::Context&);
    } suites[] = {
        {"provider",     RunProviderTests},
        {"speech_time",  RunSpeechTimeTests},
        {"srs_types",    RunSrsTypesTests},
        {"utils",        RunUtilsTests},
        {"config",       RunConfigReaderTests},
        {"pcm_queue",    RunPcmQueueTests},
        {"pcm_cache",    RunPcmCacheTests},
        {"generators",   RunGeneratorTests},
        {"srs_packet",   RunSrsPacketTests},
        {"aws_auth",     RunAwsAuthTests},
        {"edge_drm",     RunEdgeDrmTests},
        {"voice_registry", RunVoiceRegistryTests},
        {"opus_encoder", RunOpusEncoderTests},
    };

    for (const auto& suite : suites) {
        try {
            suite.run(ctx);
        } catch (const std::exception& e) {
            ctx.Fail(__FILE__, __LINE__,
                     std::string("suite ") + suite.name + " threw: " + e.what());
        } catch (...) {
            ctx.Fail(__FILE__, __LINE__,
                     std::string("suite ") + suite.name + " threw an unknown exception");
        }
    }

    return ctx.Summary();
}
