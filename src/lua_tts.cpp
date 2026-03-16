#include "lua_tts.h"
#include "lua_helpers.h"
#include "backends/srs/srs_backend.h"
#include "tts_pipeline.h"
#include "config_reader.h"
#include "speech_time.h"
#include "provider.h"
#include "utils.h"

#include <memory>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Backend factory
// ---------------------------------------------------------------------------
static HoundTTS::ITTSBackend* MakeBackend(const std::string& transmitter) {
    // if (transmitter == "discord") return new HoundTTS::DiscordBackend();
    return new HoundTTS::SRSBackend();
}

// ---------------------------------------------------------------------------
// HoundTTS.textToSpeech(message, tx, ep)
//
// message : string
// tx      : table — transmission parameters
//   .transmitter  "srs"
//   .host         string  SRS host
//   .port         number  SRS port
//   .freqs        string  e.g. "251.0"
//   .modulations  string  e.g. "AM"
//   .coalition    number  0/1/2
//   .name         string
//   .volume       number  0.0–1.0
//   .encrypt      bool
//   .encKey       number
//   .lat/.lon/.alt numbers (pre-converted by Lua)
// ep      : table — provider parameters
//   .provider     "piper"|"azure"|"google"|"elevenlabs"|"aws"|"polly"|"sapi"|"openai"|"kitten"/"kittentts" (deprecated—use "openai")
//   .voice        string
//   .speaker      string  (piper multi-speaker only)
//   .culture      string  e.g. "en-US"
//   .gender       string  "male"|"female"
//   .speed        number
//   .volume       number
//   .engine       string  (aws/polly: "standard"|"neural"|"generative")
//
// Returns: estimated speech time in seconds (number)
// ---------------------------------------------------------------------------
int l_textToSpeech(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);  // tx
    luaL_checktype(L, 3, LUA_TTABLE);  // ep

    std::string transmitter   = GetTableString(L, 2, "transmitter", "srs");
    std::string providerStr    = GetTableString(L, 3, "provider",    "sapi");
    HoundTTS::TtsProvider provider = HoundTTS::ParseTtsProvider(providerStr);

    std::unique_ptr<HoundTTS::ITTSBackend> backend(MakeBackend(transmitter));

    HoundTTS::TTSRequest req;
    req.writedir    = HoundTTS::ConfigReader::Instance().GetWritedir();
    req.transmitter = transmitter;
    req.message     = message;

    // Transmission params (arg 2)
    req.srsHost     = GetTableString(L, 2, "host",        "localhost");
    req.srsPort     = GetTableInt   (L, 2, "port",        5002);
    req.freqs       = GetTableString(L, 2, "freqs",       "251");
    req.modulations = GetTableString(L, 2, "modulations", "AM");
    req.coalition   = GetTableInt   (L, 2, "coalition",   0);
    req.name        = GetTableString(L, 2, "name",        "HoundTTS");
    req.encrypt     = GetTableBool  (L, 2, "encrypt",     false);
    req.encKey      = GetTableInt   (L, 2, "encKey",      0);
    req.lat         = GetTableNumber(L, 2, "lat",         91.0);
    req.lon         = GetTableNumber(L, 2, "lon",         181.0);
    req.alt         = GetTableNumber(L, 2, "alt",         -500.0);

    // Provider params (arg 3)
    req.provider    = provider;  // enum
    req.voice       = GetTableString(L, 3, "voice",   "");
    req.speaker     = GetTableString(L, 3, "speaker", "");
    req.culture     = GetTableString(L, 3, "culture", "");
    req.gender      = GetTableString(L, 3, "gender",  "female");
    req.speed       = GetTableNumber(L, 3, "speed",   -999.0);
    req.volume      = GetTableNumber(L, 3, "volume",  1.0);
    req.awsPollyEngine = GetTableString(L, 3, "engine",  "");

    req.isSSML = (req.message.find("<speak>") != std::string::npos);

    // Optional translation params (arg 4)
    if (lua_istable(L, 4)) {
        req.translateProvider       = HoundTTS::ParseTranslateProvider(
                                         GetTableString(L, 4, "provider", ""));
        req.translateLanguage       = GetTableString(L, 4, "language",        "");
        req.translateSourceLanguage = GetTableString(L, 4, "source_language", "en");
    }

    // Build TransmitParams from the request (pure routing — no TTS logic)
    HoundTTS::TransmitParams txParams;
    txParams.host       = req.srsHost.empty() ? "127.0.0.1" : req.srsHost;
    txParams.port       = req.srsPort;
    txParams.freqs      = req.freqs;
    txParams.modulations = req.modulations;
    txParams.encrypt    = req.encrypt;
    txParams.encKey     = static_cast<uint8_t>(req.encKey);
    txParams.coalition  = req.coalition;
    txParams.name       = req.name;
    txParams.lat        = req.lat;
    txParams.lon        = req.lon;
    txParams.alt        = req.alt;

    // Shared PCM queue: pipeline produces, backend consumes
    auto pcmQueue = std::make_shared<HoundTTS::PCMQueue>();

    // Detach a single thread: pipeline runs (may detach sub-threads), then backend transmits
    std::thread([req, pcmQueue, txParams,
                 b = std::shared_ptr<HoundTTS::ITTSBackend>(backend.release())]() {
        HoundTTS::TTSPipeline::Produce(req, pcmQueue);
        b->Transmit(pcmQueue, txParams);
    }).detach();

    double speed = req.speed;
    if (speed <= -999.0)
        speed = (provider == HoundTTS::TtsProvider::Sapi) ? 0.0 : 1.0;

    double speechTime = HoundTTS::GetSpeechTime(
        static_cast<int>(req.message.size()), speed, provider);
    lua_pushnumber(L, speechTime);
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.getSpeechTime(length, speed, provider)
// length:   number (char count) or string
// speed:    number, default 1
// provider: string — "sapi"|"google"|"gcloud"|"azure"|"aws"|"polly"|
//                    "elevenlabs"|"piper"|"openai"|"kittentts"
//           default "sapi".  Boolean coercion (legacy googleTTS) is done
//           in the Lua wrapper; the DLL always receives a string.
// Returns:  estimated speech time in seconds
// ---------------------------------------------------------------------------
int l_getSpeechTime(lua_State* L) {
    auto& log = HoundTTS::Logger::Instance();
    log.Debug("getSpeechTime", "entered, nargs=" + std::to_string(lua_gettop(L)));

    int length = 0;
    if (lua_isnumber(L, 1)) {
        length = static_cast<int>(lua_tointeger(L, 1));
        log.Debug("getSpeechTime", "arg1 number, length=" + std::to_string(length));
    } else if (lua_isstring(L, 1)) {
        size_t len = 0;
        lua_tolstring(L, 1, &len);
        length = static_cast<int>(len);
        log.Debug("getSpeechTime", "arg1 string, length=" + std::to_string(length));
    } else {
        log.Error("getSpeechTime", "arg1 bad type=" + std::to_string(lua_type(L, 1)));
        return luaL_argerror(L, 1, "expected number or string");
    }

    double speed = luaL_optnumber(L, 2, 1.0);
    HoundTTS::TtsProvider provider = HoundTTS::TtsProvider::Sapi;
    if (lua_isstring(L, 3)) {
        provider = HoundTTS::ParseTtsProvider(lua_tostring(L, 3));
    } else if (lua_isboolean(L, 3) && lua_toboolean(L, 3)) {
        provider = HoundTTS::TtsProvider::Google;   // legacy: true == googleTTS
    }
    log.Debug("getSpeechTime", "speed=" + std::to_string(speed)
              + " provider=" + std::string(HoundTTS::TtsProviderName(provider)));

    double result = HoundTTS::GetSpeechTime(length, speed, provider);
    log.Debug("getSpeechTime", "result=" + std::to_string(result));

    lua_pushnumber(L, result);
    return 1;
}
