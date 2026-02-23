#pragma once

#ifndef HOUNDTTS_SRS_TYPES_H
#define HOUNDTTS_SRS_TYPES_H

#include <string>
#include <vector>
#include <sstream>
#include <objbase.h>

namespace HoundTTS {

struct FreqMod {
    double freqHz;      // Frequency in Hz (e.g. 251.0 MHz -> 251000000.0)
    int    modulation;  // 0=AM, 1=FM
    bool   encrypt;     // true = encrypted transmission
    uint8_t encKey;     // encryption key (0-255)
};

// Infer default modulation from frequency (Hz).
// Mirrors HOUND.Utils.TTS.getdefaultModulation: FM below 90 MHz or between 1 Hz and 90 MHz
// when expressed in Hz (i.e. < 90 000 000 Hz but > 1 000 000 Hz means AM; < 90 MHz means FM).
// Rule: FM if freqHz < 90e6 (i.e. below 90 MHz), otherwise AM.
inline int DefaultModulation(double freqHz) {
    // Below 90 MHz → FM; at or above 90 MHz → AM
    return (freqHz > 0.0 && freqHz < 90000000.0) ? 1 : 0;
}

// Parse comma-separated freq (MHz) and modulation strings into FreqMod vector.
// freqs:       "251,252.5"
// modulations: "AM,FM"  — if a modulation token is missing or empty, it is inferred from the frequency.
// encrypt/encKey apply uniformly to all frequencies (per-transmission setting).
inline std::vector<FreqMod> ParseFreqMods(const std::string& freqs,
                                           const std::string& modulations,
                                           bool encrypt = false,
                                           uint8_t encKey = 0) {
    std::vector<FreqMod> result;

    // Tokenize freqs
    std::vector<std::string> freqTokens, modTokens;
    {
        std::istringstream ss(freqs);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // trim whitespace
            size_t s = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (s != std::string::npos)
                freqTokens.push_back(tok.substr(s, e - s + 1));
        }
    }
    {
        std::istringstream ss(modulations);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            size_t s = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (s != std::string::npos)
                modTokens.push_back(tok.substr(s, e - s + 1));
        }
    }

    for (size_t i = 0; i < freqTokens.size(); ++i) {
        FreqMod fm;
        try { fm.freqHz = std::stod(freqTokens[i]) * 1000000.0; }
        catch (...) { fm.freqHz = 251000000.0; }

        int mod = DefaultModulation(fm.freqHz); // infer from frequency if not specified
        if (i < modTokens.size()) {
            const std::string& m = modTokens[i];
            if      (m == "AM" || m == "am" || m == "0") mod = 0;
            else if (m == "FM" || m == "fm" || m == "1") mod = 1;
        }
        fm.modulation = mod;
        fm.encrypt    = encrypt;
        fm.encKey     = encKey;
        result.push_back(fm);
    }

    if (result.empty()) {
        result.push_back({251000000.0, 0, false, 0});
    }
    return result;
}

// Base57 alphabet used by SRS (same as shortuuid)
static const char kBase57Chars[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const int  kBase57Len = 57;
static const int  kGUIDLength = 22;

// Generate a 22-character base57 UUID string compatible with SRS GUIDs.
// Uses CoCreateGuid for the random source.
inline std::string GenerateGUID() {
    GUID guid;
    CoCreateGuid(&guid);

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

} // namespace HoundTTS

#endif // HOUNDTTS_SRS_TYPES_H
