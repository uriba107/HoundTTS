#include "lua/lua.hpp"

#include "backend.h"
#include "config_reader.h"
#include "backends/srs/srs_backend.h"
#include "speech_time.h"
#include "utils.h"

#include <memory>
#include <string>
#include <cstring>

namespace {

// Route to backend based on transmitter field.
HoundTTS::ITTSBackend* MakeBackend(const std::string& transmitter) {
    // if (transmitter == "discord") {
    //     return new HoundTTS::SRSBackend();
    // }
    return new HoundTTS::SRSBackend();
}

// Helper: get a string field from a Lua table at the given stack index
std::string GetTableString(lua_State* L, int tableIdx, const char* field, const char* def = "") {
    lua_getfield(L, tableIdx, field);
    const char* val = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    std::string result(val);
    lua_pop(L, 1);
    return result;
}

// Helper: get a number field from a Lua table at the given stack index
double GetTableNumber(lua_State* L, int tableIdx, const char* field, double def = 0.0) {
    lua_getfield(L, tableIdx, field);
    double val = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return val;
}

// Helper: get an int field from a Lua table at the given stack index
int GetTableInt(lua_State* L, int tableIdx, const char* field, int def = 0) {
    lua_getfield(L, tableIdx, field);
    int val = lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : def;
    lua_pop(L, 1);
    return val;
}

// Helper: get a bool field from a Lua table at the given stack index
bool GetTableBool(lua_State* L, int tableIdx, const char* field, bool def = false) {
    lua_getfield(L, tableIdx, field);
    bool val = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : def;
    lua_pop(L, 1);
    return val;
}

// -------------------------------------------------------------------------
// Lua-callable functions
// -------------------------------------------------------------------------

// HoundTTS.init(writedir)
// Called once after require("HoundTTS") to set the Saved Games path and
// load credentials from Config\HoundTTS-credentials.ini.
int l_init(lua_State* L) {
    const char* writedir = luaL_checkstring(L, 1);
    HoundTTS::ConfigReader::Instance().Load(std::string(writedir));
    HoundTTS::Logger::Instance().Init(std::string(writedir));
    std::string lvl = HoundTTS::ConfigReader::Instance().GetLogLevel();
    HoundTTS::Logger::Instance().SetLevel(
        (lvl == "info" || lvl == "INFO") ? HoundTTS::LogLevel::LEVEL_INFO : HoundTTS::LogLevel::LEVEL_ERROR);
    return 0;
}

// HoundTTS.textToSpeech(message, tx, ep)
//
// message : string
// tx      : table — transmission parameters
//   .transmitter  "srs" | "discord"
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
//   .provider     "piper" | "azure" | "google" | "elevenlabs" | "polly" | "sapi"
//   .voice        string
//   .speaker      string  (piper multi-speaker only)
//   .culture      string  e.g. "en-US"
//   .gender       string  "male" | "female"
//   .speed        number
//
// Returns: estimated speech time in seconds (number)
int l_textToSpeech(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);  // tx
    luaL_checktype(L, 3, LUA_TTABLE);  // ep

    // tx table is at stack index 2, ep table at index 3
    std::string transmitter = GetTableString(L, 2, "transmitter", "srs");
    std::string provider    = GetTableString(L, 3, "provider", "sapi");

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
    req.provider     = provider;
    req.voice        = GetTableString(L, 3, "voice",       "");
    req.speaker      = GetTableString(L, 3, "speaker",     "");
    req.culture      = GetTableString(L, 3, "culture",     "");
    req.gender       = GetTableString(L, 3, "gender",      "female");
    req.speed        = GetTableNumber(L, 3, "speed",       -999.0);
    req.volume       = GetTableNumber(L, 3, "volume",      1.0);
    req.pollyEngine  = GetTableString(L, 3, "engine", "");

    req.isSSML = (req.message.find("<speak>") != std::string::npos);

    bool ok = backend->TransmitTTS(req);

    if (!ok) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to dispatch TTS request");
        return 2;
    }

    double speed = req.speed;
    if (speed <= -999.0) {
        speed = (provider == "sapi" || provider == "win" || provider.empty()) ? 0.0 : 1.0;
    }
    double speechTime = HoundTTS::GetSpeechTime(
        static_cast<int>(req.message.size()), speed, provider.c_str());
    lua_pushnumber(L, speechTime);
    return 1;
}

// HoundTTS.getSpeechTime(length, speed, provider)
// length:   number (character count) or string (will use its length)
// speed:    number (interpretation depends on provider), default 1
// provider: string "sapi"|"google"|"azure"|"polly"|"elevenlabs"|"piper", default "sapi"
// Returns:  estimated speech time in seconds
int l_getSpeechTime(lua_State* L) {
    int length = 0;
    if (lua_isnumber(L, 1)) {
        length = static_cast<int>(lua_tointeger(L, 1));
    } else if (lua_isstring(L, 1)) {
        size_t len = 0;
        lua_tolstring(L, 1, &len);
        length = static_cast<int>(len);
    } else {
        return luaL_argerror(L, 1, "expected number or string");
    }

    double speed = luaL_optnumber(L, 2, 1.0);
    const char* provider = luaL_optstring(L, 3, "sapi");

    double result = HoundTTS::GetSpeechTime(length, speed, provider);
    lua_pushnumber(L, result);
    return 1;
}

// Module function table
static const luaL_Reg HoundTTS_funcs[] = {
    { "init",          l_init },
    { "textToSpeech",  l_textToSpeech },
    { "getSpeechTime", l_getSpeechTime },
    { nullptr, nullptr }
};

} // anonymous namespace

// DLL entry point for Lua: require("HoundTTS")
extern "C" __declspec(dllexport) int luaopen_HoundTTS(lua_State* L) {
    luaL_register(L, "HoundTTS", HoundTTS_funcs);
    lua_pushstring(L, HOUNDTTS_VERSION);
    lua_setfield(L, -2, "version");
    return 1;
}
