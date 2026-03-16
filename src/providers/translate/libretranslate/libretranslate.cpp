#include "libretranslate.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace HoundTTS {

static const char* kTag = "HoundTTS/LibreTranslate";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// Language name → ISO 639-1 code
// LibreTranslate uses standard ISO 639-1 codes.
// ---------------------------------------------------------------------------
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
        {"thai",       "th"}, {"hindi",      "hi"},
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

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
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

// Extract "translatedText" field value from LibreTranslate JSON response:
// {"translatedText":"<value>"}
static std::string ExtractTranslatedText(const std::string& json) {
    const std::string key = "\"translatedText\"";
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

// ---------------------------------------------------------------------------
// Parse host, port, SSL flag, and path prefix from a URL.
// ---------------------------------------------------------------------------
static bool ParseEndpoint(const std::string& endpoint,
                           bool& outSsl, std::string& outHost,
                           int& outPort, std::string& outPathPrefix)
{
    outSsl = false; outHost = "localhost"; outPort = 5000; outPathPrefix = "";
    std::string url = endpoint;
    if (url.substr(0, 8) == "https://") { outSsl = true;  url = url.substr(8); }
    else if (url.substr(0, 7) == "http://")  { outSsl = false; url = url.substr(7); }
    auto slashPos = url.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? url.substr(0, slashPos) : url;
    if (slashPos != std::string::npos) outPathPrefix = url.substr(slashPos);
    auto colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        outHost = hostPort.substr(0, colonPos);
        try { outPort = std::stoi(hostPort.substr(colonPos + 1)); }
        catch (...) { outPort = outSsl ? 443 : 5000; }
    } else {
        outHost = hostPort;
        outPort = outSsl ? 443 : 5000;
    }
    return !outHost.empty();
}

// ---------------------------------------------------------------------------
// LibreTranslate::Translate
// ---------------------------------------------------------------------------
std::string LibreTranslate::Translate(
    const std::string& text,
    const std::string& language,
    const std::string& endpoint,
    const std::string& apiKey,
    const std::string& source_language)
{
    if (endpoint.empty()) {
        LogE("LibreTranslate endpoint not configured "
             "(set [LibreTranslate] endpoint in HoundTTS-credentials.ini)");
        return "";
    }
    if (text.empty()) return "";

    std::string targetCode = LanguageToCode(language);
    if (targetCode.empty()) return "";

    bool ssl = false;
    std::string host, pathPrefix;
    int port = 5000;
    if (!ParseEndpoint(endpoint, ssl, host, port, pathPrefix)) {
        LogE("Failed to parse LibreTranslate endpoint: " + endpoint);
        return "";
    }

    std::string srcCode = source_language.empty() ? "en" : source_language;
    // Normalise: if a full name was passed (e.g. "English"), convert to code
    srcCode = LanguageToCode(srcCode);
    if (srcCode.empty()) srcCode = "en";

    // Build JSON body:
    // {"q":"<text>","source":"<src>","target":"<code>","format":"text","alternatives":0[,"api_key":"<key>"]}
    // alternatives=0 suppresses unused alternative translations.
    std::ostringstream body;
    body << "{"
         << "\"q\":\""          << JsonEscape(text) << "\","
         << "\"source\":\""    << srcCode << "\","
         << "\"target\":\""     << targetCode << "\","
         << "\"format\":\"text\","
         << "\"alternatives\":0";
    if (!apiKey.empty())
        body << ",\"api_key\":\"" << JsonEscape(apiKey) << "\"";
    body << "}";

    std::string bodyStr = body.str();
    std::string path = pathPrefix + "/translate";

    LogI("POST " + endpoint + "/translate target=" + targetCode);

    httplib::Headers headers = {{"Content-Type", "application/json"}};

    std::string responseBody;
    int responseStatus = 0;

    if (ssl) {
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(30);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) { LogE("HTTPS request failed to " + endpoint); return ""; }
        responseStatus = res->status;
        responseBody   = res->body;
    } else {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(30);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) { LogE("HTTP request failed to " + endpoint); return ""; }
        responseStatus = res->status;
        responseBody   = res->body;
    }

    if (responseStatus != 200) {
        LogE("HTTP " + std::to_string(responseStatus) + " body=[" + responseBody + "]");
        return "";
    }
    if (responseBody.empty()) { LogE("Empty response body"); return ""; }

    LogI("Received " + std::to_string(responseBody.size()) + " bytes");

    std::string translated = ExtractTranslatedText(responseBody);
    if (translated.empty()) {
        LogE("translatedText not found in response: " + responseBody.substr(0, 512));
        return "";
    }

    LogI("Translation done: " + translated);
    return translated;
}

} // namespace HoundTTS
