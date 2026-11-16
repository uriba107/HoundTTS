// AWS SigV4 signing (shared by Polly TTS and AWS Translate).
//
// Crypto regressions fail closed at "no audio", which is indistinguishable from
// a network problem in-game. The expected values below are the published AWS
// test vectors plus one full request computed independently.

#include "test_framework.h"

#include <string>
#include <vector>

#include "providers/shared/aws/aws_auth.h"

using namespace HoundTTS;

namespace {

std::string HexOf(const std::vector<unsigned char>& bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out += kHex[(b >> 4) & 0x0F];
        out += kHex[b & 0x0F];
    }
    return out;
}

} // namespace

void RunAwsAuthTests(testing::Context& ctx) {
    CASE(ctx, "SHA256Hex matches published digests") {
        CHECK_STR_EQ(ctx, AwsAuth::SHA256Hex(""),
                     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK_STR_EQ(ctx, AwsAuth::SHA256Hex("abc"),
                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    CASE(ctx, "HexEncode is lowercase and zero padded") {
        const unsigned char data[3] = {0x00, 0x0F, 0xFF};
        CHECK_STR_EQ(ctx, AwsAuth::HexEncode(data, sizeof(data)), "000fff");
        CHECK_STR_EQ(ctx, AwsAuth::HexEncode(data, 0), "");
    }

    CASE(ctx, "HmacSHA256 matches the RFC 4231 style vector") {
        const std::vector<unsigned char> mac = AwsAuth::HmacSHA256(
            "key", "The quick brown fox jumps over the lazy dog");
        CHECK_STR_EQ(ctx, HexOf(mac),
                     "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    }

    CASE(ctx, "DeriveSigningKey matches the AWS documentation example") {
        const std::vector<unsigned char> key = AwsAuth::DeriveSigningKey(
            "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "20150830", "us-east-1", "iam");
        CHECK_EQ_INT(ctx, key.size(), 32);
        CHECK_STR_EQ(ctx, HexOf(key),
                     "c4afb1cc5771d871763a393e44b703571b55cc28424d1a5e86da6ed3c154a4b9");
    }

    CASE(ctx, "DeriveSigningKey changes with region, service and date") {
        const std::string secret = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
        const std::string base =
            HexOf(AwsAuth::DeriveSigningKey(secret, "20150830", "us-east-1", "polly"));
        CHECK(ctx, base != HexOf(AwsAuth::DeriveSigningKey(secret, "20150830", "eu-west-1", "polly")));
        CHECK(ctx, base != HexOf(AwsAuth::DeriveSigningKey(secret, "20150830", "us-east-1", "translate")));
        CHECK(ctx, base != HexOf(AwsAuth::DeriveSigningKey(secret, "20150831", "us-east-1", "polly")));
    }

    CASE(ctx, "ComputeSigV4Auth builds the documented Authorization header") {
        const std::string auth = AwsAuth::ComputeSigV4Auth(
            "AKIDEXAMPLE",
            "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
            "us-east-1",
            "polly",
            "polly.us-east-1.amazonaws.com",
            "POST",
            "/v1/speech",
            "{\"Text\":\"hi\"}",
            "20240101T120000Z",
            "20240101");

        CHECK_STR_EQ(ctx, auth,
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20240101/us-east-1/polly/aws4_request, "
            "SignedHeaders=host;x-amz-date, "
            "Signature=bcabdeb3dd81c053b008d6a4b3886c5dca7320301c1635ecb536dd88c5b36642");
    }

    CASE(ctx, "ComputeSigV4Auth signature depends on the body") {
        const std::string base = AwsAuth::ComputeSigV4Auth(
            "AKID", "SECRET", "us-east-1", "polly",
            "polly.us-east-1.amazonaws.com", "POST", "/v1/speech",
            "{\"Text\":\"hi\"}", "20240101T120000Z", "20240101");
        const std::string other = AwsAuth::ComputeSigV4Auth(
            "AKID", "SECRET", "us-east-1", "polly",
            "polly.us-east-1.amazonaws.com", "POST", "/v1/speech",
            "{\"Text\":\"bye\"}", "20240101T120000Z", "20240101");
        CHECK(ctx, base != other);
        CHECK(ctx, base.rfind("AWS4-HMAC-SHA256 Credential=AKID/", 0) == 0);
    }
}
