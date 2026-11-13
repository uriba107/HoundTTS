// utils.cpp helpers — JSON escaping used for every provider request, the
// decoder used for SRS/HTTP replies, PCM byte conversion, and SRS GUID shape.

#include "test_framework.h"

#include <string>
#include <vector>

#include "utils.h"

using namespace HoundTTS;

void RunUtilsTests(testing::Context& ctx) {
    CASE(ctx, "JsonEscape escapes the short forms") {
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\""), "\\\"");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\\"), "\\\\");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\n"), "\\n");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\r"), "\\r");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\t"), "\\t");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\b"), "\\b");
        CHECK_STR_EQ(ctx, Utils::JsonEscape("\f"), "\\f");
    }

    CASE(ctx, "JsonEscape escapes remaining control bytes as \\u00XX") {
        CHECK_STR_EQ(ctx, Utils::JsonEscape(std::string(1, '\x01')), "\\u0001");
        CHECK_STR_EQ(ctx, Utils::JsonEscape(std::string(1, '\x1f')), "\\u001f");
    }

    CASE(ctx, "JsonEscape passes UTF-8 bytes through untouched") {
        const std::string utf8 = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";  // é € 😀
        CHECK_STR_EQ(ctx, Utils::JsonEscape(utf8), utf8);
    }

    CASE(ctx, "JsonEscape/JsonDecodeString round-trip") {
        const std::string original =
            "quote:\" backslash:\\ newline:\n tab:\t ctrl:\x01 utf8:\xC3\xA9";
        const std::string encoded = Utils::JsonEscape(original) + "\"";
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString(encoded, 0), original);
    }

    CASE(ctx, "JsonDecodeString handles all short escapes") {
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\"\\/\\b\\f\\n\\r\\t\"", 1), "/\b\f\n\r\t");
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\q\"", 0), "q");  // unknown escape -> literal
    }

    CASE(ctx, "JsonDecodeString combines UTF-16 surrogate pairs") {
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\uD83D\\uDE00\"", 0), "\xF0\x9F\x98\x80");
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\u00E9\"", 0), "\xC3\xA9");
    }

    CASE(ctx, "JsonDecodeString degrades malformed input to '?'") {
        // Lone high surrogate — no valid UTF-8 encoding exists
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\uD800\"", 0), "?");
        // Truncated escape at end of buffer
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\u12", 0), "?");
        // Non-hex digits: the '?' replaces the escape, the leftover literal
        // characters stay (?ZZ, not ?) — that is the documented behaviour.
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("\\u12ZZ\"", 0), "?ZZ");
    }

    CASE(ctx, "JsonDecodeString reports the closing-quote position") {
        size_t endPos = 999;
        const std::string s = "hi\"rest";
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString(s, 0, &endPos), "hi");
        CHECK_EQ_INT(ctx, endPos, 3);
    }

    CASE(ctx, "JsonDecodeString tolerates a start offset past the end") {
        size_t endPos = 0;
        CHECK_STR_EQ(ctx, Utils::JsonDecodeString("abc", 99, &endPos), "");
        CHECK_EQ_INT(ctx, endPos, 3);
    }

    CASE(ctx, "IsAlphaLanguageCode accepts 2-3 ASCII letters only") {
        CHECK(ctx, Utils::IsAlphaLanguageCode("en"));
        CHECK(ctx, Utils::IsAlphaLanguageCode("EN"));
        CHECK(ctx, Utils::IsAlphaLanguageCode("eng"));
        CHECK(ctx, !Utils::IsAlphaLanguageCode(""));
        CHECK(ctx, !Utils::IsAlphaLanguageCode("e"));
        CHECK(ctx, !Utils::IsAlphaLanguageCode("engl"));
        CHECK(ctx, !Utils::IsAlphaLanguageCode("en-US"));
        CHECK(ctx, !Utils::IsAlphaLanguageCode("e1"));
        CHECK(ctx, !Utils::IsAlphaLanguageCode("\xC3\xA9n"));  // non-ASCII é
    }

    CASE(ctx, "PcmBytesToSamples copies whole samples and ignores a trailing byte") {
        const unsigned char raw[5] = {0x01, 0x00, 0x02, 0x00, 0x03};
        const std::vector<int16_t> s = Utils::PcmBytesToSamples(raw, sizeof(raw));
        CHECK_EQ_INT(ctx, s.size(), 2);
        CHECK_EQ_INT(ctx, s[0], 1);
        CHECK_EQ_INT(ctx, s[1], 2);
    }

    CASE(ctx, "PcmBytesToSamples handles zero bytes and null input") {
        CHECK_EQ_INT(ctx, Utils::PcmBytesToSamples(nullptr, 0).size(), 0);
        const std::vector<int16_t> z = Utils::PcmBytesToSamples(nullptr, 4);
        CHECK_EQ_INT(ctx, z.size(), 2);
        CHECK_EQ_INT(ctx, z[0], 0);
        CHECK_EQ_INT(ctx, z[1], 0);
    }

    CASE(ctx, "GenerateSRSGuid yields 22 base57 chars, unique per call") {
        const std::string a = Utils::GenerateSRSGuid();
        const std::string b = Utils::GenerateSRSGuid();
        CHECK_EQ_INT(ctx, a.size(), 22);
        CHECK_EQ_INT(ctx, b.size(), 22);
        CHECK(ctx, a != b);

        const std::string alphabet =
            "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
        CHECK(ctx, a.find_first_not_of(alphabet) == std::string::npos);
        CHECK(ctx, b.find_first_not_of(alphabet) == std::string::npos);
    }
}
