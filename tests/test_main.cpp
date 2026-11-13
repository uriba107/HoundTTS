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

    RunProviderTests(ctx);
    RunSpeechTimeTests(ctx);
    RunSrsTypesTests(ctx);
    RunUtilsTests(ctx);
    RunConfigReaderTests(ctx);
    RunPcmQueueTests(ctx);
    RunPcmCacheTests(ctx);
    RunGeneratorTests(ctx);
    RunSrsPacketTests(ctx);
    RunAwsAuthTests(ctx);
    RunEdgeDrmTests(ctx);
    RunVoiceRegistryTests(ctx);
    RunOpusEncoderTests(ctx);

    return ctx.Summary();
}
