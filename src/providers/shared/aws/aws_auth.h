#pragma once

#ifndef HOUNDTTS_AWS_AUTH_H
#define HOUNDTTS_AWS_AUTH_H

#include <string>
#include <vector>

namespace HoundTTS {
namespace AwsAuth {

// Hex-encode raw bytes.
std::string HexEncode(const unsigned char* data, size_t len);

// SHA-256 hash of data, returned as lowercase hex.
std::string SHA256Hex(const std::string& data);

// HMAC-SHA256 with a byte-vector key.
std::vector<unsigned char> HmacSHA256(const std::vector<unsigned char>& key,
                                       const std::string& data);

// HMAC-SHA256 with a string key (convenience overload).
std::vector<unsigned char> HmacSHA256(const std::string& key,
                                       const std::string& data);

// Derive the AWS SigV4 signing key for (secretKey, date, region, service).
// date must be in YYYYMMDD format.
std::vector<unsigned char> DeriveSigningKey(const std::string& secretKey,
                                             const std::string& date,
                                             const std::string& region,
                                             const std::string& service);

// Build the full Authorization header value for an AWS SigV4 request.
// accessKey  : AWS access key ID
// secretKey  : AWS secret access key
// region     : AWS region e.g. "us-east-1"
// service    : AWS service name e.g. "polly", "translate"
// host       : full hostname e.g. "polly.us-east-1.amazonaws.com"
// method     : HTTP method e.g. "POST"
// uri        : request path e.g. "/v1/speech"
// body       : raw request body (used for payload hash)
// amzDate    : ISO 8601 timestamp e.g. "20240101T120000Z"
// dateStamp  : date-only e.g. "20240101"
std::string ComputeSigV4Auth(const std::string& accessKey,
                              const std::string& secretKey,
                              const std::string& region,
                              const std::string& service,
                              const std::string& host,
                              const std::string& method,
                              const std::string& uri,
                              const std::string& body,
                              const std::string& amzDate,
                              const std::string& dateStamp);

} // namespace AwsAuth
} // namespace HoundTTS

#endif // HOUNDTTS_AWS_AUTH_H
