#include "utils.h"
#include <objbase.h>
#include <string>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <vector>

namespace HoundTTS {
namespace Utils {

// ---------------------------------------------------------------------------
// JSON helpers (shared by all HTTP providers)
// ---------------------------------------------------------------------------
static int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void AppendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string JsonEscape(const std::string& s) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += kHex[(c >> 4) & 0x0F];
                out += kHex[c & 0x0F];
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

std::string JsonDecodeString(const std::string& s, size_t pos, size_t* endPos) {
    std::string out;
    if (pos > s.size()) pos = s.size();
    out.reserve(s.size() - pos);

    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') { ++pos; break; }
        if (c != '\\') { out += c; ++pos; continue; }
        if (pos + 1 >= s.size()) { ++pos; break; }

        char e = s[pos + 1];
        pos += 2;
        switch (e) {
        case '"':  out += '"';  break;
        case '\\': out += '\\'; break;
        case '/':  out += '/';  break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        case 'u': {
            if (pos + 4 > s.size()) { out += '?'; break; }
            unsigned int cp = 0;
            bool ok = true;
            for (int i = 0; i < 4; ++i) {
                int v = HexDigit(s[pos + i]);
                if (v < 0) { ok = false; break; }
                cp = (cp << 4) | static_cast<unsigned int>(v);
            }
            if (!ok) { out += '?'; break; }
            pos += 4;

            // Combine a UTF-16 surrogate pair into one code point.
            if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() &&
                s[pos] == '\\' && s[pos + 1] == 'u') {
                unsigned int lo = 0;
                bool ok2 = true;
                for (int i = 0; i < 4; ++i) {
                    int v = HexDigit(s[pos + 2 + i]);
                    if (v < 0) { ok2 = false; break; }
                    lo = (lo << 4) | static_cast<unsigned int>(v);
                }
                if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    pos += 6;
                }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) {
                out += '?';   // lone surrogate — no valid UTF-8 encoding
            } else {
                AppendUtf8(out, cp);
            }
            break;
        }
        default:
            out += e;
            break;
        }
    }

    if (endPos) *endPos = pos;
    return out;
}

bool IsAlphaLanguageCode(const std::string& code) {
    if (code.size() < 2 || code.size() > 3) return false;
    for (unsigned char c : code) {
        if (std::isalpha(c) == 0) return false;
    }
    return true;
}

std::vector<int16_t> PcmBytesToSamples(const void* data, size_t byteCount) {
    const size_t count = byteCount / sizeof(int16_t);
    std::vector<int16_t> samples(count);
    if (count > 0 && data != nullptr) {
        std::memcpy(samples.data(), data, count * sizeof(int16_t));
    }
    return samples;
}

// Base57 alphabet used by SRS (same as shortuuid)
static const char kBase57Chars[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const int  kBase57Len = 57;
static const int  kGUIDLength = 22;

std::string GenerateSRSGuid() {
    GUID guid;
    HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr)) {
        Logger::Instance().Error("Utils::GenerateSRSGuid",
            "CoCreateGuid failed with HRESULT " + std::to_string(hr));
        return std::string();
    }

    // Treat the 16 GUID bytes as a 128-bit big-endian integer
    unsigned char bytes[16];
    bytes[0]  = (guid.Data1 >> 24) & 0xFF;
    bytes[1]  = (guid.Data1 >> 16) & 0xFF;
    bytes[2]  = (guid.Data1 >>  8) & 0xFF;
    bytes[3]  =  guid.Data1        & 0xFF;
    bytes[4]  = (guid.Data2 >>  8) & 0xFF;
    bytes[5]  =  guid.Data2        & 0xFF;
    bytes[6]  = (guid.Data3 >>  8) & 0xFF;
    bytes[7]  =  guid.Data3        & 0xFF;
    for (int i = 0; i < 8; ++i) bytes[8 + i] = guid.Data4[i];

    // Convert to base57: treat bytes as big-endian 128-bit number
    // Use 64-bit arithmetic in two halves
    // Represent as array of uint32 digits in base 2^32 (big-endian, 4 limbs)
    uint32_t limbs[4];
    limbs[0] = (uint32_t)bytes[0]  << 24 | (uint32_t)bytes[1]  << 16 |
               (uint32_t)bytes[2]  <<  8 | (uint32_t)bytes[3];
    limbs[1] = (uint32_t)bytes[4]  << 24 | (uint32_t)bytes[5]  << 16 |
               (uint32_t)bytes[6]  <<  8 | (uint32_t)bytes[7];
    limbs[2] = (uint32_t)bytes[8]  << 24 | (uint32_t)bytes[9]  << 16 |
               (uint32_t)bytes[10] <<  8 | (uint32_t)bytes[11];
    limbs[3] = (uint32_t)bytes[12] << 24 | (uint32_t)bytes[13] << 16 |
               (uint32_t)bytes[14] <<  8 | (uint32_t)bytes[15];

    // Repeated division by 57 to get base57 digits (least significant first)
    char digits[kGUIDLength];
    for (int d = 0; d < kGUIDLength; ++d) {
        // Divide 128-bit number (limbs[0..3]) by 57, store remainder
        uint64_t rem = 0;
        for (int j = 0; j < 4; ++j) {
            uint64_t cur = (rem << 32) | limbs[j];
            limbs[j] = (uint32_t)(cur / kBase57Len);
            rem      = cur % kBase57Len;
        }
        digits[d] = kBase57Chars[rem];
    }

    // Reverse to get most-significant digit first
    std::string result(kGUIDLength, '0');
    for (int i = 0; i < kGUIDLength; ++i)
        result[i] = digits[kGUIDLength - 1 - i];

    return result;
}

} // namespace Utils
} // namespace HoundTTS
