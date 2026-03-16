#include "google_auth.h"
#include "utils.h"

#include "httplib.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <string>
#include <sstream>
#include <fstream>
#include <ctime>

namespace HoundTTS {
namespace GoogleAuth {

static const char* kTag = "HoundTTS/GoogleAuth";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }

// ---------------------------------------------------------------------------
// Minimal JSON field extractor
// ---------------------------------------------------------------------------
std::string JsonString(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + key.size());
    if (pos == std::string::npos) return "";
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            if      (e == 'n')  out += '\n';
            else if (e == 'r')  out += '\r';
            else if (e == 't')  out += '\t';
            else                out += e;
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON escaping
// ---------------------------------------------------------------------------
std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Base64url encode (no padding) — for JWT header/payload
// ---------------------------------------------------------------------------
static const char kB64UrlChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string Base64UrlEncode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int b = (unsigned int)data[i] << 16;
        if (i + 1 < len) b |= (unsigned int)data[i+1] << 8;
        if (i + 2 < len) b |= (unsigned int)data[i+2];
        out += kB64UrlChars[(b >> 18) & 0x3F];
        out += kB64UrlChars[(b >> 12) & 0x3F];
        if (i + 1 < len) out += kB64UrlChars[(b >> 6) & 0x3F];
        if (i + 2 < len) out += kB64UrlChars[(b     ) & 0x3F];
    }
    return out;
}

static std::string Base64UrlEncodeStr(const std::string& s) {
    return Base64UrlEncode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// ---------------------------------------------------------------------------
// RS256 JWT signature
// ---------------------------------------------------------------------------
static std::string RS256Sign(const std::string& data, const std::string& pemKey) {
    BIO* bio = BIO_new_mem_buf(pemKey.data(), static_cast<int>(pemKey.size()));
    if (!bio) return "";

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        LogE("Failed to parse RSA private key from service-account JSON");
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::string sig;

    if (ctx &&
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        EVP_DigestSignUpdate(ctx,
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size()) == 1)
    {
        size_t sigLen = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) == 1) {
            sig.resize(sigLen);
            EVP_DigestSignFinal(ctx,
                reinterpret_cast<unsigned char*>(&sig[0]), &sigLen);
            sig.resize(sigLen);
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return sig;
}

// ---------------------------------------------------------------------------
// ReadServiceAccount
// ---------------------------------------------------------------------------
bool ReadServiceAccount(const std::string& credsFile,
                        std::string& outEmail,
                        std::string& outKey)
{
    std::ifstream f(Utils::Utf8ToWide(credsFile).c_str());
    if (!f.is_open()) {
        LogE("Cannot open credentials file: " + credsFile);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    outEmail = JsonString(json, "client_email");
    outKey   = JsonString(json, "private_key");

    if (outEmail.empty() || outKey.empty()) {
        LogE("credentials file missing client_email or private_key. "
             "Expected a Google service-account JSON file.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GetAccessToken
// ---------------------------------------------------------------------------
std::string GetAccessToken(const std::string& clientEmail,
                           const std::string& privateKey,
                           const std::string& scope)
{
    std::string header = R"({"alg":"RS256","typ":"JWT"})";

    long now = static_cast<long>(std::time(nullptr));
    std::ostringstream claims;
    claims << "{"
           << "\"iss\":\"" << clientEmail << "\","
           << "\"scope\":\"" << scope << "\","
           << "\"aud\":\"https://oauth2.googleapis.com/token\","
           << "\"iat\":" << now << ","
           << "\"exp\":" << (now + 3600)
           << "}";

    std::string signingInput = Base64UrlEncodeStr(header) + "." +
                               Base64UrlEncodeStr(claims.str());

    std::string sig = RS256Sign(signingInput, privateKey);
    if (sig.empty()) return "";

    std::string jwt = signingInput + "." +
                      Base64UrlEncode(reinterpret_cast<const unsigned char*>(sig.data()),
                                      sig.size());

    httplib::SSLClient cli("oauth2.googleapis.com", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);

    std::string body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
                       "&assertion=" + jwt;

    auto res = cli.Post("/token", body, "application/x-www-form-urlencoded");
    if (!res || res->status != 200) {
        LogE("OAuth2 token exchange failed" +
             (res ? " HTTP " + std::to_string(res->status) + " body=[" + res->body + "]" : ""));
        return "";
    }

    std::string token = JsonString(res->body, "access_token");
    if (token.empty())
        LogE("access_token not found in OAuth2 response");
    return token;
}

} // namespace GoogleAuth
} // namespace HoundTTS
