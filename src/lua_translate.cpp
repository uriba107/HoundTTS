#include "lua_translate.h"
#include "lua_helpers.h"
#include "config_reader.h"
#include "providers/translate/openai/openai_chat.h"
#include "providers/translate/google/google_translate.h"
#include "providers/translate/libretranslate/libretranslate.h"
#include "providers/translate/aws/aws_translate.h"
#include "providers/translate/azure/azure_translate.h"

#include "provider.h"
#include "utils.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

static const char* kTag = "HoundTTS/Translate";
static void LogD(const std::string& msg) { HoundTTS::Logger::Instance().Debug(kTag, msg); }
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }

// ---------------------------------------------------------------------------
// Async translation infrastructure
// ---------------------------------------------------------------------------
namespace {

struct TranslateResult {
    bool        done  = false;
    std::string text;   // translated text (empty on error)
    std::string error;  // non-empty on failure
};

static std::atomic<int>                                          s_nextId{1};
static std::mutex                                                s_trMutex;
static std::unordered_map<int, std::shared_ptr<TranslateResult>> s_trResults;

} // namespace

// ---------------------------------------------------------------------------
// HoundTTS.translateAsync(message, params)
//
// message : string — text to translate
// params  : table
//   .provider         "openai" | "google" ("gcloud") | "libretranslate" ("libre") | "aws" ("polly") | "azure"
//   .language         ISO 639-1 target code e.g. "de" (or full name e.g. "German")
//   .source_language  ISO 639-1 source code, default "en" (LibreTranslate only)
//
// Returns: integer request ID (immediately, non-blocking).
// ---------------------------------------------------------------------------
int l_translateAsync(lua_State* L) {
    LogD("translateAsync: entered");
    const char* message = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    std::string providerStr     = GetTableString(L, 2, "provider",        "openai");
    std::string language         = GetTableString(L, 2, "language",        "en");
    std::string source_language  = GetTableString(L, 2, "source_language", "en");
    HoundTTS::TranslateProvider provider = HoundTTS::ParseTranslateProvider(providerStr);

    LogD("translateAsync: provider=" + std::string(HoundTTS::TranslateProviderName(provider)) + " lang=" + language + " src=" + source_language);

    int id = s_nextId.fetch_add(1);
    auto slot = std::make_shared<TranslateResult>();
    {
        std::lock_guard<std::mutex> lk(s_trMutex);
        s_trResults[id] = slot;
    }
    LogD("translateAsync: slot created, id=" + std::to_string(id));

    std::string msg(message);
    std::string lang(language);
    std::string src_lang(source_language);
    auto& cfg = HoundTTS::ConfigReader::Instance();

    if (provider == HoundTTS::TranslateProvider::OpenAI) {
        std::string endpoint  = cfg.GetOpenAIEndpoint();
        std::string apiKey    = cfg.GetOpenAIKey();
        std::string chatModel = cfg.GetOpenAIChatModel();
        LogD("translateAsync: openai endpoint=" + endpoint + " model=" + chatModel);

        std::thread([slot, msg, lang, endpoint, apiKey, chatModel]() {
            LogD("translateAsync/openai thread: started");
            std::string translated = HoundTTS::OpenAIChat::Translate(
                msg, lang, endpoint, apiKey, chatModel);
            slot->error = translated.empty()
                ? "Translation failed (check Logs\\HoundTTS.log for details)" : "";
            slot->text  = translated.empty() ? "" : std::move(translated);
            slot->done  = true;
            LogD("translateAsync/openai thread: done, success=" + std::to_string(!translated.empty()));
        }).detach();
        LogD("translateAsync: openai thread detached");

    } else if (provider == HoundTTS::TranslateProvider::Google) {
        std::string credsFile = cfg.GetGoogleCredsFile();
        LogD("translateAsync: google credsFile=" + credsFile);

        std::thread([slot, msg, lang, credsFile]() {
            LogD("translateAsync/google thread: started");
            std::string translated = HoundTTS::GoogleTranslate::Translate(
                msg, lang, credsFile);
            slot->error = translated.empty()
                ? "Translation failed (check Logs\\HoundTTS.log for details)" : "";
            slot->text  = translated.empty() ? "" : std::move(translated);
            slot->done  = true;
            LogD("translateAsync/google thread: done, success=" + std::to_string(!translated.empty()));
        }).detach();
        LogD("translateAsync: google thread detached");

    } else if (provider == HoundTTS::TranslateProvider::LibreTranslate) {
        std::string endpoint = cfg.GetLibreTranslateEndpoint();
        std::string apiKey   = cfg.GetLibreTranslateApiKey();
        LogD("translateAsync: libre endpoint=" + endpoint);

        std::thread([slot, msg, lang, src_lang, endpoint, apiKey]() {
            LogD("translateAsync/libre thread: started");
            std::string translated = HoundTTS::LibreTranslate::Translate(
                msg, lang, endpoint, apiKey, src_lang);
            slot->error = translated.empty()
                ? "Translation failed (check Logs\\HoundTTS.log for details)" : "";
            slot->text  = translated.empty() ? "" : std::move(translated);
            slot->done  = true;
            LogD("translateAsync/libre thread: done, success=" + std::to_string(!translated.empty()));
        }).detach();
        LogD("translateAsync: libre thread detached");

    } else if (provider == HoundTTS::TranslateProvider::AWS) {
        std::string accessKey = cfg.GetAwsAccessKey();
        std::string secretKey = cfg.GetAwsSecretKey();
        std::string region    = cfg.GetAwsRegion();
        LogD("translateAsync: aws region=" + region);

        std::thread([slot, msg, lang, accessKey, secretKey, region]() {
            LogD("translateAsync/aws thread: started");
            std::string translated = HoundTTS::AwsTranslate::Translate(
                msg, lang, accessKey, secretKey, region);
            slot->error = translated.empty()
                ? "Translation failed (check Logs\\HoundTTS.log for details)" : "";
            slot->text  = translated.empty() ? "" : std::move(translated);
            slot->done  = true;
            LogD("translateAsync/aws thread: done, success=" + std::to_string(!translated.empty()));
        }).detach();
        LogD("translateAsync: aws thread detached");

    } else if (provider == HoundTTS::TranslateProvider::Azure) {
        std::string azureKey    = cfg.GetAzureKey();
        std::string azureRegion = cfg.GetAzureRegion();
        LogD("translateAsync: azure region=" + azureRegion);

        std::thread([slot, msg, lang, azureKey, azureRegion]() {
            LogD("translateAsync/azure thread: started");
            std::string translated = HoundTTS::AzureTranslate::Translate(
                msg, lang, azureKey, azureRegion);
            slot->error = translated.empty()
                ? "Translation failed (check Logs\\HoundTTS.log for details)" : "";
            slot->text  = translated.empty() ? "" : std::move(translated);
            slot->done  = true;
            LogD("translateAsync/azure thread: done, success=" + std::to_string(!translated.empty()));
        }).detach();
        LogD("translateAsync: azure thread detached");

    } else {
        LogE("translateAsync: unknown provider '" + std::string(HoundTTS::TranslateProviderName(provider)) + "'");
        // Unknown provider — remove slot and return error immediately
        {
            std::lock_guard<std::mutex> lk(s_trMutex);
            s_trResults.erase(id);
        }
        lua_pushnil(L);
        lua_pushstring(L, ("Unsupported translation provider: " + providerStr).c_str());
        return 2;
    }

    LogD("translateAsync: returning id=" + std::to_string(id));
    lua_pushinteger(L, id);
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.getTranslationResult(id)
//
// id : integer — request ID returned by translateAsync
//
// Returns:
//   nil                  — still pending
//   translated_string    — success
//   nil, error_string    — failure
//
// Completed results are removed from storage after retrieval.
// ---------------------------------------------------------------------------
int l_getTranslationResult(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    LogD("getTranslationResult: id=" + std::to_string(id));

    std::shared_ptr<TranslateResult> slot;
    {
        std::lock_guard<std::mutex> lk(s_trMutex);
        auto it = s_trResults.find(id);
        if (it == s_trResults.end()) {
            lua_pushnil(L);
            lua_pushstring(L, "Unknown translation request ID");
            return 2;
        }
        slot = it->second;
    }

    if (!slot->done) {
        LogD("getTranslationResult: id=" + std::to_string(id) + " still pending");
        lua_pushnil(L);
        return 1;
    }
    LogD("getTranslationResult: id=" + std::to_string(id) + " done");

    {
        std::lock_guard<std::mutex> lk(s_trMutex);
        s_trResults.erase(id);
    }

    if (!slot->error.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, slot->error.c_str());
        return 2;
    }

    lua_pushstring(L, slot->text.c_str());
    return 1;
}
