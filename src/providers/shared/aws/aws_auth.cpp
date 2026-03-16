#include "aws_auth.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace HoundTTS {
namespace AwsAuth {

std::string HexEncode(const unsigned char* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

std::string SHA256Hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return HexEncode(hash, SHA256_DIGEST_LENGTH);
}

std::vector<unsigned char> HmacSHA256(const std::vector<unsigned char>& key,
                                       const std::string& data)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int outLen = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         out, &outLen);
    return std::vector<unsigned char>(out, out + outLen);
}

std::vector<unsigned char> HmacSHA256(const std::string& key,
                                       const std::string& data)
{
    std::vector<unsigned char> keyVec(key.begin(), key.end());
    return HmacSHA256(keyVec, data);
}

std::vector<unsigned char> DeriveSigningKey(const std::string& secretKey,
                                             const std::string& date,
                                             const std::string& region,
                                             const std::string& service)
{
    auto kDate    = HmacSHA256("AWS4" + secretKey, date);
    auto kRegion  = HmacSHA256(kDate,              region);
    auto kService = HmacSHA256(kRegion,            service);
    auto kSigning = HmacSHA256(kService,           "aws4_request");
    return kSigning;
}

std::string ComputeSigV4Auth(const std::string& accessKey,
                              const std::string& secretKey,
                              const std::string& region,
                              const std::string& service,
                              const std::string& host,
                              const std::string& method,
                              const std::string& uri,
                              const std::string& body,
                              const std::string& amzDate,
                              const std::string& dateStamp)
{
    std::string canonicalHeaders =
        "host:" + host + "\n"
        "x-amz-date:" + amzDate + "\n";
    std::string signedHeaders = "host;x-amz-date";
    std::string bodyHash = SHA256Hex(body);
    std::string canonicalRequest =
        method + "\n" + uri + "\n" + "\n" +
        canonicalHeaders + "\n" + signedHeaders + "\n" + bodyHash;
    std::string credentialScope = dateStamp + "/" + region + "/" + service + "/aws4_request";
    std::string stringToSign =
        "AWS4-HMAC-SHA256\n" + amzDate + "\n" +
        credentialScope + "\n" + SHA256Hex(canonicalRequest);
    auto signingKey = DeriveSigningKey(secretKey, dateStamp, region, service);
    auto sigBytes   = HmacSHA256(signingKey, stringToSign);
    std::string signature = HexEncode(sigBytes.data(), sigBytes.size());
    return "AWS4-HMAC-SHA256 "
           "Credential=" + accessKey + "/" + credentialScope + ", "
           "SignedHeaders=" + signedHeaders + ", "
           "Signature=" + signature;
}

} // namespace AwsAuth
} // namespace HoundTTS
