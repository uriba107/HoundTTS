#include "lua_tts.h"
#include "lua_helpers.h"
#include "backends/srs/srs_backend.h"
#include "tts_pipeline.h"
#include "backends/pcm_cache.h"
#include "session.h"
#include "config_reader.h"
#include "speech_time.h"
#include "provider.h"
#include "utils.h"

#include <memory>
#include <string>
#include <thread>
#include <random>
#include <chrono>
#include <sstream>
#include <set>
#include <cmath>
#include <vector>

#include <objbase.h>
#include <functional>
#include <mutex>

// ---------------------------------------------------------------------------
// Backend factory
// ---------------------------------------------------------------------------
static HoundTTS::ITTSBackend* MakeBackend(const std::string& transmitter) {
    // if (transmitter == "discord") return new HoundTTS::DiscordBackend();
    return new HoundTTS::SRSBackend();
}

// ---------------------------------------------------------------------------
// ExpandFreqs — frequency spread / leakage simulation
//
// Parses a comma-separated MHz string (e.g. "251.0,305.0") and for each
// center frequency generates adjacent channels spaced stepKhz apart out to
// ±spreadKhz. Results are deduplicated by rounded-Hz key so overlapping
// peaks (e.g. "251.0,252.0") never double-transmit the same channel.
//
// SRS FreqCloseEnough threshold = ±500 Hz, so stepKhz >= 1.0 keeps channels
// audibly distinct. Defaults: stepKhz=25 (standard AM spacing), spreadKhz=250
// (±10 channels = ±250 kHz around each center).
// ---------------------------------------------------------------------------
static void ExpandFreqs(const std::string& freqsIn,
                        const std::string& modsIn,
                        double             spreadKhz,
                        double             stepKhz,
                        std::string&       freqsOut,
                        std::string&       modsOut)
{
    std::vector<double>      centers;
    std::vector<std::string> mods;
    {
        std::stringstream sf(freqsIn), sm(modsIn);
        std::string tok;
        while (std::getline(sf, tok, ','))
            try { centers.push_back(std::stod(tok)); } catch (...) {}
        while (std::getline(sm, tok, ','))
            mods.push_back(tok);
    }
    if (centers.empty() ||
        !std::isfinite(spreadKhz) || !std::isfinite(stepKhz) ||
        spreadKhz <= 0.0 || stepKhz <= 0.0) {
        freqsOut = freqsIn; modsOut = modsIn; return;
    }

    // Enforce a minimum step (1 Hz) to avoid division-by-zero or absurd step counts
    // and a hard cap on steps to prevent unbounded loops / memory growth if the
    // caller passes extreme inputs.
    static constexpr double kMinStepKhz = 1e-3; // 1 Hz
    static constexpr int    kMaxSteps   = 10000;
    if (stepKhz < kMinStepKhz) stepKhz = kMinStepKhz;

    const double spreadHz = spreadKhz * 1000.0;
    const double stepHz   = stepKhz   * 1000.0;

    std::set<long long>                        seen;
    std::vector<std::pair<double,std::string>> result;

    auto tryAdd = [&](double freqMhz, const std::string& mod) {
        if (!std::isfinite(freqMhz)) return;
        long long key = static_cast<long long>(std::round(freqMhz * 1e6));
        if (seen.insert(key).second)
            result.emplace_back(freqMhz, mod);
    };

    for (size_t ci = 0; ci < centers.size(); ++ci) {
        double      cMhz = centers[ci];
        std::string mod  = (ci < mods.size()) ? mods[ci]
                         : mods.empty() ? "AM" : mods.back();
        tryAdd(cMhz, mod);
        double stepsD = std::floor(spreadHz / stepHz);
        if (!std::isfinite(stepsD) || stepsD < 0.0) stepsD = 0.0;
        if (stepsD > static_cast<double>(kMaxSteps)) stepsD = static_cast<double>(kMaxSteps);
        int steps = static_cast<int>(stepsD);
        for (int s = 1; s <= steps; ++s) {
            double off = (stepHz * s) / 1e6;
            tryAdd(cMhz + off, mod);
            tryAdd(cMhz - off, mod);
        }
    }

    std::ostringstream sf, sm;
    for (size_t i = 0; i < result.size(); ++i) {
        if (i > 0) { sf << ','; sm << ','; }
        sf << result[i].first;
        sm << result[i].second;
    }
    freqsOut = sf.str();
    modsOut  = sm.str();
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
//   .provider     "piper"|"azure"|"google"|"elevenlabs"|"aws"|"polly"|"sapi"|"openai"|"edge"|"edgetts"
//   .voice        string
//   .speaker      string  (piper multi-speaker only)
//   .culture      string  e.g. "en-US"
//   .gender       string  "male"|"female"
//   .speed        number
//   .volume       number
//   .engine       string  (aws/polly: "standard"|"neural"|"generative")
//
// Returns: speechTime (number), sessionId (string)
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
    req.srsHost          = GetTableString(L, 2, "host",               "localhost");
    req.srsPort          = GetTableInt   (L, 2, "port",               5002);
    req.freqs            = GetTableString(L, 2, "freqs",              "251");
    req.modulations      = GetTableString(L, 2, "modulations",        "AM");
    req.coalition        = GetTableInt   (L, 2, "coalition",          0);
    req.name             = GetTableString(L, 2, "name",               "HoundTTS");
    req.encrypt          = GetTableBool  (L, 2, "encrypt",            false);
    req.encKey           = GetTableInt   (L, 2, "encKey",             0);
    req.srsBluePassword  = GetTableString(L, 2, "srs_blue_password",  "");
    req.srsRedPassword   = GetTableString(L, 2, "srs_red_password",   "");
    req.lat              = GetTableNumber(L, 2, "lat",                91.0);
    req.lon              = GetTableNumber(L, 2, "lon",                181.0);
    req.alt              = GetTableNumber(L, 2, "alt",                -500.0);

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

    // Create and register a session for this transmission
    std::string sessionId = HoundTTS::Utils::GenerateSRSGuid();
    if (sessionId.empty()) {
        auto& log = HoundTTS::Logger::Instance();
        log.Error("l_textToSpeech", "failed to generate session GUID");
        lua_pushnil(L);
        lua_pushstring(L, "Failed to generate session GUID");
        return 2;
    }
    auto session = HoundTTS::SessionManager::Instance().Register(sessionId);

    // Build TransmitParams from the request (pure routing — no TTS logic)
    HoundTTS::TransmitParams txParams;
    txParams.host            = req.srsHost.empty() ? "127.0.0.1" : req.srsHost;
    txParams.port            = req.srsPort;
    txParams.freqs           = req.freqs;
    txParams.modulations     = req.modulations;
    txParams.encrypt         = req.encrypt;
    txParams.encKey          = static_cast<uint8_t>(req.encKey);
    txParams.coalition       = req.coalition;
    txParams.name            = req.name;
    txParams.srsBluePassword = req.srsBluePassword;
    txParams.srsRedPassword  = req.srsRedPassword;
    txParams.lat             = req.lat;
    txParams.lon             = req.lon;
    txParams.alt             = req.alt;
    txParams.session         = session;

    // Shared PCM queue: pipeline produces, backend consumes
    auto pcmQueue = std::make_shared<HoundTTS::PCMQueue>();

    // Detach a single thread: pipeline runs (may detach sub-threads), then backend transmits
    std::thread([req, pcmQueue, txParams,
                 b = std::shared_ptr<HoundTTS::ITTSBackend>(backend.release())]() {
        HoundTTS::TTSPipeline::Produce(req, pcmQueue);
        b->Transmit(pcmQueue, txParams);
    }).detach();

    // Exact duration on cache hit, heuristic estimate on miss.
    // Peek is cheap (mutex + map lookup); TTSPipeline::Produce will re-check
    // on the detached thread independently.
    double speechTime = -1.0;
    if (req.message != "__test_tone__") {
        uint64_t cacheKey = HoundTTS::ComputeTTSRequestKey(req);
        auto cached = HoundTTS::PCMCache::Instance().Get(cacheKey);
        if (cached) {
            speechTime = static_cast<double>(cached->size()) / 16000.0;
        }
    }
    if (speechTime < 0.0) {
        double speed = req.speed;
        if (speed <= -999.0)
            speed = (provider == HoundTTS::TtsProvider::Sapi) ? 0.0 : 1.0;
        speechTime = HoundTTS::GetSpeechTime(
            static_cast<int>(req.message.size()), speed, provider)
            + 2 * HoundTTS::PTT_PAD_SEC; // silence padding before + after speech
    }

    lua_pushnumber(L, speechTime);
    lua_pushstring(L, sessionId.c_str());
    return 2;
}

// ---------------------------------------------------------------------------
// HoundTTS.getSpeechTime(length, speed, provider)
// length:   number (char count) or string
// speed:    number, default 1
// provider: string — "sapi"|"google"|"gcloud"|"azure"|"aws"|"polly"|
//                    "elevenlabs"|"piper"|"openai"
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

// ---------------------------------------------------------------------------
// HoundTTS.startNoise(txParams, noiseParams) → sessionId
//
// txParams  : table — same shape as textToSpeech arg 2
// noiseParams : table
//   .noiseType  string  "white" (default) | "chirp" | "harsh" | "jam"
//   .volume     number  0.0–1.0 (default 1.0)
//   .seed       number  RNG seed (optional, auto-generated if absent)
//
// Returns: sessionId (string)
// ---------------------------------------------------------------------------
int l_startNoise(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);  // tx
    luaL_checktype(L, 2, LUA_TTABLE);  // noiseParams

    // Transmission params
    std::string transmitter       = GetTableString(L, 1, "transmitter",       "srs");
    std::string host              = GetTableString(L, 1, "host",              "localhost");
    int         port              = GetTableInt   (L, 1, "port",              5002);
    std::string freqs             = GetTableString(L, 1, "freqs",             "251.0");
    std::string modulations       = GetTableString(L, 1, "modulations",       "AM");
    int         coalition         = GetTableInt   (L, 1, "coalition",         0);
    std::string name              = GetTableString(L, 1, "name",              "HoundTTS-Jammer");
    std::string srsBluePassword   = GetTableString(L, 1, "srs_blue_password", "");
    std::string srsRedPassword    = GetTableString(L, 1, "srs_red_password",  "");
    // encrypt/encKey intentionally not read from Lua — noise is never encrypted
    double      lat               = GetTableNumber(L, 1, "lat",               91.0);
    double      lon               = GetTableNumber(L, 1, "lon",               181.0);
    double      alt               = GetTableNumber(L, 1, "alt",               -500.0);

    // Noise params
    std::string noiseType  = GetTableString(L, 2, "noiseType",  "white");
    float       volume     = static_cast<float>(GetTableNumber(L, 2, "volume", 1.0));
    double      duration   = GetTableNumber(L, 2, "duration",   0.0);  // <=0 = continuous
    double      spreadKhz  = GetTableNumber(L, 2, "spreadKhz", 250.0); // ±bandwidth; 0 = off
    double      stepKhz    = GetTableNumber(L, 2, "stepKhz",    25.0); // channel spacing

    // Expand freqs/modulations to simulate RF leakage across adjacent channels
    std::string expandedFreqs = freqs;
    std::string expandedMods  = modulations;
    ExpandFreqs(freqs, modulations, spreadKhz, stepKhz, expandedFreqs, expandedMods);

    // Seed: explicit or time-based
    uint32_t seed;
    lua_getfield(L, 2, "seed");
    if (lua_isnumber(L, -1)) {
        seed = static_cast<uint32_t>(lua_tointeger(L, -1));
    } else {
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        seed = static_cast<uint32_t>(t ^ (t >> 32));
    }
    lua_pop(L, 1);

    // Register session
    std::string sessionId = HoundTTS::Utils::GenerateSRSGuid();
    if (sessionId.empty()) {
        auto& log = HoundTTS::Logger::Instance();
        log.Error("l_startNoise", "failed to generate session GUID");
        lua_pushnil(L);
        lua_pushstring(L, "Failed to generate session GUID");
        return 2;
    }
    auto session = HoundTTS::SessionManager::Instance().Register(sessionId);

    // Build TransmitParams
    HoundTTS::TransmitParams txParams;
    txParams.host            = host.empty() ? "127.0.0.1" : host;
    txParams.port            = port;
    txParams.freqs           = expandedFreqs;
    txParams.modulations     = expandedMods;
    txParams.encrypt         = false;  // noise is never encrypted (encryption would just garble the noise)
    txParams.encKey          = 0;
    txParams.coalition       = coalition;
    txParams.name            = name;
    txParams.srsBluePassword = srsBluePassword;
    txParams.srsRedPassword  = srsRedPassword;
    txParams.lat             = lat;
    txParams.lon             = lon;
    txParams.alt             = alt;
    txParams.session         = session;

    // Shared PCM queue: noise generator produces, backend consumes
    auto pcmQueue = std::make_shared<HoundTTS::PCMQueue>();

    std::unique_ptr<HoundTTS::ITTSBackend> backend(MakeBackend(transmitter));

    // Detach thread: noise generator runs concurrently with backend transmitting
    std::thread([pcmQueue, txParams, noiseType, seed, volume, duration, session,
                 b = std::shared_ptr<HoundTTS::ITTSBackend>(backend.release())]() {
        std::thread([pcmQueue, session, noiseType, seed, volume, duration]() {
            HoundTTS::TTSPipeline::ProduceNoise(pcmQueue, session, noiseType, seed, volume, duration);
        }).detach();
        b->Transmit(pcmQueue, txParams);
    }).detach();

    lua_pushstring(L, sessionId.c_str());
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.startTone(txParams, toneParams) → sessionId
//
// txParams  : table — same shape as startNoise / textToSpeech arg 2
// toneParams : table
//   .duration  number  seconds (default 2.0; <=0 defaults to 2.0)
//   .freqHz    number  Hz (default 440.0)
//   .volume    number  0.0–1.0 (default 1.0)
//
// Returns: sessionId (string)
// ---------------------------------------------------------------------------
int l_startTone(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);  // tx
    luaL_checktype(L, 2, LUA_TTABLE);  // toneParams

    // Transmission params
    std::string transmitter       = GetTableString(L, 1, "transmitter",       "srs");
    std::string host              = GetTableString(L, 1, "host",              "localhost");
    int         port              = GetTableInt   (L, 1, "port",              5002);
    std::string freqs             = GetTableString(L, 1, "freqs",             "251.0");
    std::string modulations       = GetTableString(L, 1, "modulations",       "AM");
    int         coalition         = GetTableInt   (L, 1, "coalition",         0);
    std::string name              = GetTableString(L, 1, "name",              "HoundTTS-Tone");
    std::string srsBluePassword   = GetTableString(L, 1, "srs_blue_password", "");
    std::string srsRedPassword    = GetTableString(L, 1, "srs_red_password",  "");
    // encrypt/encKey intentionally not read from Lua — tone is never encrypted
    double      lat               = GetTableNumber(L, 1, "lat",               91.0);
    double      lon               = GetTableNumber(L, 1, "lon",               181.0);
    double      alt               = GetTableNumber(L, 1, "alt",               -500.0);

    // Tone params
    double duration = GetTableNumber(L, 2, "duration", 2.0);
    float  freqHz   = static_cast<float>(GetTableNumber(L, 2, "freqHz", 440.0));
    float  volume   = static_cast<float>(GetTableNumber(L, 2, "volume", 1.0));

    // Register session (tone is finite — session lets caller kill early if needed)
    std::string sessionId = HoundTTS::Utils::GenerateSRSGuid();
    if (sessionId.empty()) {
        auto& log = HoundTTS::Logger::Instance();
        log.Error("l_startTone", "failed to generate session GUID");
        lua_pushnil(L);
        lua_pushstring(L, "Failed to generate session GUID");
        return 2;
    }
    auto session = HoundTTS::SessionManager::Instance().Register(sessionId);

    // Build TransmitParams
    HoundTTS::TransmitParams txParams;
    txParams.host            = host.empty() ? "127.0.0.1" : host;
    txParams.port            = port;
    txParams.freqs           = freqs;
    txParams.modulations     = modulations;
    txParams.encrypt         = false; // Tone should not be encrypted
    txParams.encKey          = 0;
    txParams.coalition       = coalition;
    txParams.name            = name;
    txParams.srsBluePassword = srsBluePassword;
    txParams.srsRedPassword  = srsRedPassword;
    txParams.lat             = lat;
    txParams.lon             = lon;
    txParams.alt             = alt;
    txParams.session         = session;

    auto pcmQueue = std::make_shared<HoundTTS::PCMQueue>();
    std::unique_ptr<HoundTTS::ITTSBackend> backend(MakeBackend(transmitter));

    std::thread([pcmQueue, txParams, duration, freqHz, volume, session,
                 b = std::shared_ptr<HoundTTS::ITTSBackend>(backend.release())]() {
        std::thread([pcmQueue, session, duration, freqHz, volume]() {
            HoundTTS::TTSPipeline::ProduceTone(pcmQueue, session, duration, freqHz, volume);
        }).detach();
        b->Transmit(pcmQueue, txParams);
    }).detach();

    lua_pushstring(L, sessionId.c_str());
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.updateSession(sessionId, updateParams) → boolean
//
// sessionId    : string — from startNoise or textToSpeech
// updateParams : table  — all fields optional
//   .lat   number  — new latitude
//   .lon   number  — new longitude
//   .alt   number  — new altitude
//   .alive bool    — set false to stop/kill the session
//
// Returns: true if session found AND still alive (transmission ongoing)
//          false if session found but transmission has ended (naturally or killed)
//          nil   if session not found
// ---------------------------------------------------------------------------
int l_updateSession(lua_State* L) {
    auto& log = HoundTTS::Logger::Instance();
    const char* id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    auto session = HoundTTS::SessionManager::Instance().Get(std::string(id));
    if (!session) {
        log.Debug("updateSession", "session not found: " + std::string(id));
        lua_pushnil(L);
        return 1;
    }

    // Position update — SRS-specific; only fires if backend attached SRSPositionData
    std::shared_ptr<HoundTTS::SRSPositionData> posData;
    { std::lock_guard<std::mutex> lk(session->backendMutex);
      posData = std::static_pointer_cast<HoundTTS::SRSPositionData>(session->backendData); }
    if (posData) {
        bool hasPos = false;
        double newLat = 0, newLon = 0, newAlt = 0;
        lua_getfield(L, 2, "lat");
        if (lua_isnumber(L, -1)) { newLat = lua_tonumber(L, -1); posData->lat.store(newLat); hasPos = true; }
        lua_pop(L, 1);

        lua_getfield(L, 2, "lon");
        if (lua_isnumber(L, -1)) { newLon = lua_tonumber(L, -1); posData->lon.store(newLon); hasPos = true; }
        lua_pop(L, 1);

        lua_getfield(L, 2, "alt");
        if (lua_isnumber(L, -1)) { newAlt = lua_tonumber(L, -1); posData->alt.store(newAlt); hasPos = true; }
        lua_pop(L, 1);

        if (hasPos) {
            log.Debug("updateSession", "session " + std::string(id)
                      + " position: lat=" + std::to_string(newLat)
                      + " lon=" + std::to_string(newLon)
                      + " alt=" + std::to_string(newAlt));
        }

        // If any position field changed and a sync callback is registered, fire it immediately.
        // Invoke under syncMutex so the SRSClient teardown (which also acquires
        // syncMutex before clearing the callback) cannot race with a callback
        // that captures `this` by raw pointer; otherwise the lambda could fire
        // on a destroyed SRSClient. The callback itself only issues a single
        // short TCP JSON frame so holding the mutex is bounded.
        if (hasPos) {
            std::lock_guard<std::mutex> lk(posData->syncMutex);
            if (posData->sendPositionSync)
                posData->sendPositionSync();
        }
    }

    // Kill signal — backend-agnostic; all backends honour session->alive
    lua_getfield(L, 2, "alive");
    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
        session->alive.store(false);
        log.Debug("updateSession", "session " + std::string(id) + " killed");
    }
    lua_pop(L, 1);

    bool alive = session->alive.load();
    log.Debug("updateSession", "session " + std::string(id) + " updated, alive=" + (alive ? "true" : "false"));

    // Return alive state — false signals the transmission has ended (naturally or killed)
    lua_pushboolean(L, alive ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.killAllSessions() → number
//
// Kills every active session.  Returns the number of sessions killed.
// Called from Lua on mission end to ensure no transmissions survive a restart.
// ---------------------------------------------------------------------------
int l_killAllSessions(lua_State* L) {
    auto& mgr = HoundTTS::SessionManager::Instance();
    int killed = mgr.KillAll();
    lua_pushinteger(L, killed);
    return 1;
}

// ---------------------------------------------------------------------------
// HoundTTS.clearPCMCache() → nil
//
// Drops all cached PCM entries and resets hit/miss stats.
// Call from onMissionEnd hook or whenever a full cache flush is desired.
// ---------------------------------------------------------------------------
int l_clearPCMCache(lua_State* L) {
    (void)L;
    HoundTTS::PCMCache::Instance().Clear();
    return 0;
}

// ---------------------------------------------------------------------------
// HoundTTS.onMissionEnd() → nil
//
// Mission-end cleanup: clears PCM cache and asks COM to unload idle in-process
// server DLLs (e.g. MSTTSEngine_OneCore.dll) so the next mission starts clean.
// ---------------------------------------------------------------------------
int l_onMissionEnd(lua_State* L) {
    (void)L;
    HoundTTS::PCMCache::Instance().Clear();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        CoFreeUnusedLibrariesEx(0, 0);
    }
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// HoundTTS.getCacheStats() → table
//
// Returns: { entries=N, bytes=N, hits=N, misses=N, insertions=N, evictions=N }
// ---------------------------------------------------------------------------
int l_getCacheStats(lua_State* L) {
    auto stats = HoundTTS::PCMCache::Instance().GetStats();
    lua_newtable(L);

    lua_pushinteger(L, static_cast<lua_Integer>(stats.entries));
    lua_setfield(L, -2, "entries");

    lua_pushinteger(L, static_cast<lua_Integer>(stats.bytes));
    lua_setfield(L, -2, "bytes");

    lua_pushinteger(L, static_cast<lua_Integer>(stats.hits));
    lua_setfield(L, -2, "hits");

    lua_pushinteger(L, static_cast<lua_Integer>(stats.misses));
    lua_setfield(L, -2, "misses");

    lua_pushinteger(L, static_cast<lua_Integer>(stats.insertions));
    lua_setfield(L, -2, "insertions");

    lua_pushinteger(L, static_cast<lua_Integer>(stats.evictions));
    lua_setfield(L, -2, "evictions");

    return 1;
}
