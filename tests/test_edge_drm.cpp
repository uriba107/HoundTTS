// Edge TTS DRM tokens. Edge is a free, reverse-engineered provider: when
// Microsoft rotates the token format the failure is a 403 deep inside a
// WebSocket open, so the token shape and URL contract are pinned here.

#include "test_framework.h"

#include <stdexcept>
#include <string>

#include "providers/tts/edge/edge_drm.h"

using namespace HoundTTS;

namespace {

bool IsUpperHex(const std::string& s) {
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

void RunEdgeDrmTests(testing::Context& ctx) {
    CASE(ctx, "GenerateSecMsGec is a 64-char uppercase SHA256 digest") {
        const std::string token = EdgeDRM::GenerateSecMsGec();
        CHECK_EQ_INT(ctx, token.size(), 64);
        CHECK(ctx, IsUpperHex(token));
    }

    CASE(ctx, "SecMsGecVersion embeds the pinned Chromium version") {
        CHECK_STR_EQ(ctx, EdgeDRM::SecMsGecVersion(),
                     std::string("1-") + EDGE_CHROMIUM_FULL_VERSION);
    }

    CASE(ctx, "GenerateMuid is 32 uppercase hex chars") {
        const std::string muid = EdgeDRM::GenerateMuid();
        CHECK_EQ_INT(ctx, muid.size(), 32);
        CHECK(ctx, IsUpperHex(muid));
        CHECK(ctx, muid != EdgeDRM::GenerateMuid());
    }

    CASE(ctx, "BuildWebSocketUrl carries every query parameter the server needs") {
        const std::string id = "0123456789ABCDEF0123456789ABCDEF";
        const std::string url = EdgeDRM::BuildWebSocketUrl(id);

        CHECK(ctx, url.rfind("wss://speech.platform.bing.com/consumer/speech/synthesize/"
                             "readaloud/edge/v1?", 0) == 0);
        CHECK(ctx, Contains(url, std::string("TrustedClientToken=") + EDGE_TRUSTED_CLIENT_TOKEN));
        CHECK(ctx, Contains(url, "&ConnectionId=" + id));
        CHECK(ctx, Contains(url, "&Sec-MS-GEC="));
        CHECK(ctx, Contains(url, "&Sec-MS-GEC-Version=" + EdgeDRM::SecMsGecVersion()));
    }

    CASE(ctx, "BuildWebSocketUrl rejects a malformed connection id") {
        bool threwShort = false;
        try { EdgeDRM::BuildWebSocketUrl("ABC"); }
        catch (const std::invalid_argument&) { threwShort = true; }
        CHECK(ctx, threwShort);

        bool threwLower = false;
        try { EdgeDRM::BuildWebSocketUrl(std::string(32, 'a')); }  // lowercase hex not allowed
        catch (const std::invalid_argument&) { threwLower = true; }
        CHECK(ctx, threwLower);

        bool threwJunk = false;
        try { EdgeDRM::BuildWebSocketUrl(std::string(32, 'Z')); }
        catch (const std::invalid_argument&) { threwJunk = true; }
        CHECK(ctx, threwJunk);
    }

    CASE(ctx, "BuildUserAgent and BuildCookieHeader match the Edge surface") {
        CHECK(ctx, Contains(EdgeDRM::BuildUserAgent(), "Edg/" + std::string(EDGE_CHROMIUM_MAJOR_VERSION)));
        CHECK(ctx, Contains(EdgeDRM::BuildUserAgent(), "Chrome/" + std::string(EDGE_CHROMIUM_MAJOR_VERSION)));
        CHECK_STR_EQ(ctx, EdgeDRM::BuildOrigin(),
                     "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");

        const std::string cookie = EdgeDRM::BuildCookieHeader();
        CHECK(ctx, cookie.rfind("muid=", 0) == 0);
        CHECK(ctx, cookie.back() == ';');
        CHECK_EQ_INT(ctx, cookie.size(), 5u + 32u + 1u);
    }

    CASE(ctx, "AdjustClockSkew shifts the corrected timestamp") {
        const double before = EdgeDRM::GetCorrectedTimestamp();
        EdgeDRM::AdjustClockSkew(100.0);
        const double after = EdgeDRM::GetCorrectedTimestamp();
        EdgeDRM::AdjustClockSkew(-100.0);   // restore for later tests

        CHECK(ctx, after - before >= 90.0);
    }
}
