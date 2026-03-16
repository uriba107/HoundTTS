#include "aws_translate.h"
#include "providers/shared/aws/aws_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <ctime>

namespace HoundTTS {

static const char* kTag = "HoundTTS/AwsTranslate";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

static std::string LanguageToCode(const std::string& language) {
    static const std::unordered_map<std::string, std::string> kMap = {
        {"english",    "en"}, {"german",     "de"}, {"french",     "fr"},
        {"spanish",    "es"}, {"italian",    "it"}, {"portuguese", "pt"},
        {"russian",    "ru"}, {"dutch",      "nl"}, {"polish",     "pl"},
        {"swedish",    "sv"}, {"norwegian",  "no"}, {"danish",     "da"},
        {"finnish",    "fi"}, {"greek",      "el"}, {"romanian",   "ro"},
        {"hungarian",  "hu"}, {"czech",      "cs"}, {"slovak",     "sk"},
        {"bulgarian",  "bg"}, {"croatian",   "hr"}, {"slovenian",  "sl"},
        {"turkish",    "tr"}, {"ukrainian",  "uk"}, {"arabic",     "ar"},
        {"hebrew",     "he"}, {"chinese",    "zh"}, {"japanese",   "ja"},
        {"korean",     "ko"}, {"indonesian", "id"}, {"vietnamese", "vi"},
        {"thai",       "th"}, {"hindi",      "hi"}, {"malay",      "ms"},
    };

    std::string lower = language;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    auto it = kMap.find(lower);
    if (it != kMap.end()) return it->second;

    // If already a short code (2–3 chars), pass through
    if (lower.size() >= 2 && lower.size() <= 3) return lower;

    LogE("Unknown language name: \"" + language + "\" — pass an ISO 639-1 code or add it to the map");
    return "";
}

static std::string JsonEscape(const std::string& s) {
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

// Extract "TranslatedText" field from Amazon Translate JSON response:
// {"TranslatedText":"<value>","SourceLanguageCode":"en","TargetLanguageCode":"de"}
static std::string ExtractTranslatedText(const std::string& json) {
    const std::string key = "\"TranslatedText\"";
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
            if      (e == 'n') out += '\n';
            else if (e == 'r') out += '\r';
            else if (e == 't') out += '\t';
            else if (e == 'u' && pos + 3 < json.size()) { out += '?'; pos += 4; }
            else               out += e;
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

std::string AwsTranslate::Translate(
    const std::string& text,
    const std::string& language,
    const std::string& accessKey,
    const std::string& secretKey,
    const std::string& region)
{
    if (accessKey.empty() || secretKey.empty() || region.empty()) {
        LogE("AWS access_key, secret_key, or region not configured ([AWS] section in HoundTTS-credentials.ini)");
        return "";
    }
    if (text.empty()) return "";

    std::string targetCode = LanguageToCode(language);
    if (targetCode.empty()) return "";

    std::ostringstream bodyStream;
    bodyStream << "{"
               << "\"Text\":\""               << JsonEscape(text) << "\","
               << "\"SourceLanguageCode\":\"auto\","
               << "\"TargetLanguageCode\":\"" << targetCode << "\""
               << "}";
    std::string body = bodyStream.str();

    std::time_t now = std::time(nullptr);
    std::tm utc = {};
#if defined(_MSC_VER)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char amzDateBuf[17], dateStampBuf[9];
    std::strftime(amzDateBuf,   sizeof(amzDateBuf),   "%Y%m%dT%H%M%SZ", &utc);
    std::strftime(dateStampBuf, sizeof(dateStampBuf), "%Y%m%d",         &utc);
    std::string amzDate   = amzDateBuf;
    std::string dateStamp = dateStampBuf;

    std::string host = "translate." + region + ".amazonaws.com";
    std::string authHeader = AwsAuth::ComputeSigV4Auth(
        accessKey, secretKey, region, "translate", host,
        "POST", "/", body, amzDate, dateStamp);

    httplib::SSLClient cli(host, 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = {
        {"Authorization",  authHeader},
        {"X-Amz-Date",     amzDate},
        {"Content-Type",   "application/x-amz-json-1.1"},
        {"X-Amz-Target",   "AWSShineFrontendService_20170701.TranslateText"},
        {"User-Agent",     "HoundTTS"}
    };

    LogI("Translating to " + targetCode + " via Amazon Translate (" + region + ")");

    auto res = cli.Post("/", headers, body, "application/x-amz-json-1.1");
    if (!res) { LogE("HTTP request failed (connection error)"); return ""; }
    if (res->status != 200) {
        LogE("HTTP " + std::to_string(res->status) + " body=[" + res->body + "]");
        return "";
    }

    std::string translated = ExtractTranslatedText(res->body);
    if (translated.empty()) {
        LogE("TranslatedText not found in response: " + res->body.substr(0, 512));
        return "";
    }

    LogI("Translation done: " + translated);
    return translated;
}

} // namespace HoundTTS
