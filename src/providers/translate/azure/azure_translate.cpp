#include "azure_translate.h"
#include "providers/shared/azure/azure_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace HoundTTS {

static const char* kTag = "HoundTTS/AzureTranslate";
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

// Extract first "text" field from Azure Translator JSON response:
// [{"translations":[{"text":"<value>","to":"de"}]}]
static std::string ExtractTranslatedText(const std::string& json) {
    const std::string key = "\"text\"";
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

std::string AzureTranslate::Translate(
    const std::string& text,
    const std::string& language,
    const std::string& key,
    const std::string& region)
{
    if (key.empty()) {
        LogE("Azure key not configured ([Azure] key in HoundTTS-credentials.ini)");
        return "";
    }
    if (text.empty()) return "";

    std::string targetCode = LanguageToCode(language);
    if (targetCode.empty()) return "";

    std::string body = "[{\"Text\":\"" + JsonEscape(text) + "\"}]";

    std::string path = "/translate?api-version=3.0&to=" + targetCode;

    LogI("Translating to " + targetCode + " via Azure Translator");

    httplib::SSLClient cli("api.cognitive.microsofttranslator.com", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = AzureAuth::MakeHeaders(key, region);
    headers.emplace("Content-Type", "application/json");

    auto res = cli.Post(path.c_str(), headers, body, "application/json");
    if (!res) { LogE("HTTP request failed (connection error)"); return ""; }
    if (res->status != 200) {
        LogE("HTTP " + std::to_string(res->status) + " body=[" + res->body + "]");
        return "";
    }

    std::string translated = ExtractTranslatedText(res->body);
    if (translated.empty()) {
        LogE("translated text not found in response: " + res->body.substr(0, 512));
        return "";
    }

    LogI("Translation done: " + translated);
    return translated;
}

} // namespace HoundTTS
