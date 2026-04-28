#include "utils.h"
#include <objbase.h>
#include <string>
#include <cstdint>

namespace HoundTTS {
namespace Utils {

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
