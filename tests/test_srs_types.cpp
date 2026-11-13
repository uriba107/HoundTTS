// SRS frequency/modulation parsing (srs_types.h) — decides which radio channels
// the transmission actually lands on, and the AM/FM flag the server sees.

#include "test_framework.h"

#include <vector>

#include "backends/srs/srs_types.h"

using namespace HoundTTS;

void RunSrsTypesTests(testing::Context& ctx) {
    CASE(ctx, "DefaultModulation: FM below 90 MHz, AM at/above, AM for junk") {
        CHECK_EQ_INT(ctx, DefaultModulation(251000000.0), 0);   // 251 MHz -> AM
        CHECK_EQ_INT(ctx, DefaultModulation(121500000.0), 0);   // 121.5 MHz -> AM
        CHECK_EQ_INT(ctx, DefaultModulation(89999999.0), 1);    // just below 90 MHz -> FM
        CHECK_EQ_INT(ctx, DefaultModulation(90000000.0), 0);    // exactly 90 MHz -> AM
        CHECK_EQ_INT(ctx, DefaultModulation(30000000.0), 1);    // 30 MHz -> FM
        CHECK_EQ_INT(ctx, DefaultModulation(0.0), 0);
        CHECK_EQ_INT(ctx, DefaultModulation(-5.0), 0);
    }

    CASE(ctx, "ParseFreqMods parses parallel freq/modulation lists") {
        std::vector<FreqMod> f = ParseFreqMods("251,252.5", "AM,FM");
        CHECK_EQ_INT(ctx, f.size(), 2);
        CHECK_NEAR(ctx, f[0].freqHz, 251000000.0, 1e-6);
        CHECK_EQ_INT(ctx, f[0].modulation, 0);
        CHECK_NEAR(ctx, f[1].freqHz, 252500000.0, 1e-6);
        CHECK_EQ_INT(ctx, f[1].modulation, 1);
        CHECK(ctx, !f[0].encrypt);
        CHECK_EQ_INT(ctx, f[0].encKey, 0);
    }

    CASE(ctx, "ParseFreqMods infers modulation when the token is missing") {
        // The 90 MHz rule splits the DCS bands: 30 MHz is VHF/FM, 251 MHz is AM.
        std::vector<FreqMod> f = ParseFreqMods("251,30", "");
        CHECK_EQ_INT(ctx, f.size(), 2);
        CHECK_EQ_INT(ctx, f[0].modulation, 0);   // 251 MHz -> AM
        CHECK_EQ_INT(ctx, f[1].modulation, 1);   // 30 MHz -> FM

        // Short modulation list: later entries fall back to inference
        std::vector<FreqMod> g = ParseFreqMods("251,30", "FM");
        CHECK_EQ_INT(ctx, g.size(), 2);
        CHECK_EQ_INT(ctx, g[0].modulation, 1);   // explicit
        CHECK_EQ_INT(ctx, g[1].modulation, 1);   // inferred FM
    }

    CASE(ctx, "ParseFreqMods accepts numeric and lower-case modulation tokens") {
        std::vector<FreqMod> f = ParseFreqMods("251,251,251,251", "am,FM,0,1");
        CHECK_EQ_INT(ctx, f.size(), 4);
        CHECK_EQ_INT(ctx, f[0].modulation, 0);
        CHECK_EQ_INT(ctx, f[1].modulation, 1);
        CHECK_EQ_INT(ctx, f[2].modulation, 0);
        CHECK_EQ_INT(ctx, f[3].modulation, 1);
    }

    CASE(ctx, "ParseFreqMods trims whitespace and skips empty tokens") {
        std::vector<FreqMod> f = ParseFreqMods(" 251 ,, 252.5 ", " AM ,, FM ");
        CHECK_EQ_INT(ctx, f.size(), 2);
        CHECK_NEAR(ctx, f[0].freqHz, 251000000.0, 1e-6);
        CHECK_EQ_INT(ctx, f[0].modulation, 0);
        CHECK_NEAR(ctx, f[1].freqHz, 252500000.0, 1e-6);
        CHECK_EQ_INT(ctx, f[1].modulation, 1);
    }

    CASE(ctx, "ParseFreqMods falls back to 251 MHz AM for unparseable input") {
        std::vector<FreqMod> f = ParseFreqMods("not-a-number", "FM");
        CHECK_EQ_INT(ctx, f.size(), 1);
        CHECK_NEAR(ctx, f[0].freqHz, 251000000.0, 1e-6);

        std::vector<FreqMod> empty = ParseFreqMods("", "");
        CHECK_EQ_INT(ctx, empty.size(), 1);
        CHECK_NEAR(ctx, empty[0].freqHz, 251000000.0, 1e-6);
        CHECK_EQ_INT(ctx, empty[0].modulation, 0);
    }

    CASE(ctx, "ParseFreqMods applies encrypt/encKey to every entry") {
        std::vector<FreqMod> f = ParseFreqMods("251,252", "AM,AM", true, 42);
        CHECK_EQ_INT(ctx, f.size(), 2);
        for (const auto& fm : f) {
            CHECK(ctx, fm.encrypt);
            CHECK_EQ_INT(ctx, fm.encKey, 42);
        }
    }
}
