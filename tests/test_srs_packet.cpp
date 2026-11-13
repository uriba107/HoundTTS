// SRS wire format — the UDP voice packet layout and the handshake JSON.
//
// The packet layout is copied from the SRS server's Go implementation; a byte
// offset slip still "works" (the build passes, the DLL loads) but the server
// drops the audio or puts it on the wrong frequency. These checks are the only
// place that offset contract is written down in code.

#include "test_framework.h"

#include <cstring>
#include <string>
#include <vector>

#include "backends/srs/srs_client.h"
#include "backends/srs/srs_types.h"

using namespace HoundTTS;

namespace {

uint16_t Rd16(const std::vector<uint8_t>& v, size_t o) {
    return static_cast<uint16_t>(v[o]) | (static_cast<uint16_t>(v[o + 1]) << 8);
}

uint32_t Rd32(const std::vector<uint8_t>& v, size_t o) {
    return static_cast<uint32_t>(v[o]) |
           (static_cast<uint32_t>(v[o + 1]) << 8) |
           (static_cast<uint32_t>(v[o + 2]) << 16) |
           (static_cast<uint32_t>(v[o + 3]) << 24);
}

uint64_t Rd64(const std::vector<uint8_t>& v, size_t o) {
    uint64_t out = 0;
    for (int i = 7; i >= 0; --i) {
        out = (out << 8) | static_cast<uint64_t>(v[o + static_cast<size_t>(i)]);
    }
    return out;
}

double RdDouble(const std::vector<uint8_t>& v, size_t o) {
    double d = 0.0;
    std::memcpy(&d, v.data() + o, sizeof(d));
    return d;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

void RunSrsPacketTests(testing::Context& ctx) {
    CASE(ctx, "BuildVoicePacket lays out header, audio, frequencies and fixed block") {
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});
        freqs.push_back({121500000.0, 1, true, 7});

        const std::vector<uint8_t> audio = {0xAA, 0xBB, 0xCC};
        const std::string guid = "ABCDEFGHIJKLMNOPQRSTUV";  // 22 chars

        const std::vector<uint8_t> pkt =
            SRSClient::BuildVoicePacket(audio, freqs, 2, 0x0102030405060708ULL, guid);

        // 6 header + 3 audio + 2*10 freqs + 57 fixed
        CHECK_EQ_INT(ctx, pkt.size(), 86u);
        CHECK_EQ_INT(ctx, Rd16(pkt, 0), 86);   // PacketLength
        CHECK_EQ_INT(ctx, Rd16(pkt, 2), 3);    // AudioSegLen
        CHECK_EQ_INT(ctx, Rd16(pkt, 4), 20);   // FreqSegLen

        CHECK_EQ_INT(ctx, pkt[6], 0xAA);
        CHECK_EQ_INT(ctx, pkt[7], 0xBB);
        CHECK_EQ_INT(ctx, pkt[8], 0xCC);

        // Frequency entry 1: f64 Hz, modulation byte, encryption-key byte
        CHECK_NEAR(ctx, RdDouble(pkt, 9), 251000000.0, 1e-6);
        CHECK_EQ_INT(ctx, pkt[17], 0);         // AM
        CHECK_EQ_INT(ctx, pkt[18], 0);         // not encrypted -> key forced to 0

        // Frequency entry 2
        CHECK_NEAR(ctx, RdDouble(pkt, 19), 121500000.0, 1e-6);
        CHECK_EQ_INT(ctx, pkt[27], 1);         // FM
        CHECK_EQ_INT(ctx, pkt[28], 7);         // encryption key

        CHECK_EQ_INT(ctx, Rd32(pkt, 29), 2u);  // UnitID
        CHECK(ctx, Rd64(pkt, 33) == 0x0102030405060708ULL);
        CHECK_EQ_INT(ctx, pkt[41], 0);         // Hops

        CHECK_STR_EQ(ctx, std::string(pkt.begin() + 42, pkt.begin() + 64), guid);
        CHECK_STR_EQ(ctx, std::string(pkt.begin() + 64, pkt.begin() + 86), guid);
    }

    CASE(ctx, "BuildVoicePacket zero-pads a short GUID") {
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});

        const std::vector<uint8_t> pkt =
            SRSClient::BuildVoicePacket({0x00}, freqs, 0, 0, "ABC");

        CHECK_EQ_INT(ctx, pkt.size(), 6u + 1u + 10u + 57u);
        // Fixed block starts at 6 + audio(1) + freqs(10) = 17; RelayGUID at +13.
        const size_t guidStart = 6 + 1 + 10 + 4 + 8 + 1;
        CHECK_STR_EQ(ctx, std::string(pkt.begin() + guidStart, pkt.begin() + guidStart + 3), "ABC");
        CHECK_EQ_INT(ctx, pkt[guidStart + 3], 0);   // zero-padded to 22 bytes
    }

    CASE(ctx, "BuildVoicePacket tolerates an empty audio frame") {
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});
        const std::vector<uint8_t> pkt =
            SRSClient::BuildVoicePacket({}, freqs, 1, 1, std::string(22, 'A'));
        CHECK_EQ_INT(ctx, pkt.size(), 6u + 0u + 10u + 57u);
        CHECK_EQ_INT(ctx, Rd16(pkt, 2), 0);
        CHECK_EQ_INT(ctx, Rd16(pkt, 0), 73);
    }

    CASE(ctx, "BuildClientInfoJson maps unset position sentinels to 0/0/0") {
        SRSClient client;
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});

        const std::string json = client.BuildClientInfoJson(2, "HoundTTS", freqs);
        CHECK(ctx, Contains(json,
            "\"LatLngPosition\":{\"lat\":0.000000,\"lng\":0.000000,\"alt\":0.0}"));
        CHECK(ctx, Contains(json, "\"Coalition\":2"));
        CHECK(ctx, Contains(json, "\"ClientGuid\":\""));
    }

    CASE(ctx, "BuildClientInfoJson emits real coordinates when set") {
        SRSClient client;
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});

        const std::string json = client.BuildClientInfoJson(1, "tx", freqs, 45.5, -30.25, 1000.0);
        CHECK(ctx, Contains(json,
            "\"LatLngPosition\":{\"lat\":45.500000,\"lng\":-30.250000,\"alt\":1000.0}"));
    }

    CASE(ctx, "BuildClientInfoJson serialises radios with the SRS field names") {
        SRSClient client;
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});
        freqs.push_back({121500000.0, 1, true, 9});

        const std::string json = client.BuildClientInfoJson(0, "tx", freqs);
        CHECK(ctx, Contains(json, "\"RadioInfo\":{\"radios\":["));
        CHECK(ctx, Contains(json,
            "{\"freq\":251000000.0,\"modulation\":0,\"enc\":false,\"encKey\":0,"
            "\"secFreq\":0.0,\"retransmit\":false}"));
        CHECK(ctx, Contains(json,
            "{\"freq\":121500000.0,\"modulation\":1,\"enc\":true,\"encKey\":9,"
            "\"secFreq\":0.0,\"retransmit\":false}"));
        CHECK(ctx, Contains(json, "\"iff\":{\"control\":2"));
    }

    CASE(ctx, "BuildClientInfoJson escapes quotes in the transmitter name") {
        SRSClient client;
        std::vector<FreqMod> freqs;
        freqs.push_back({251000000.0, 0, false, 0});

        const std::string json = client.BuildClientInfoJson(0, "a\"b", freqs);
        CHECK(ctx, Contains(json, "\"Name\":\"a\\\"b\""));
    }

    CASE(ctx, "BuildClientMetadataJson omits RadioInfo but keeps position") {
        SRSClient client;

        const std::string json = client.BuildClientMetadataJson(2, "tx", 45.5, -30.25, 1000.0);
        CHECK(ctx, Contains(json,
            "\"LatLngPosition\":{\"lat\":45.500000,\"lng\":-30.250000,\"alt\":1000.0}"));
        CHECK(ctx, !Contains(json, "RadioInfo"));
        CHECK(ctx, Contains(json, "\"Seat\":0"));
        CHECK(ctx, Contains(json, "\"AllowRecord\":true"));
    }
}
