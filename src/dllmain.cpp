#include "lua/lua.hpp"
#include "config_reader.h"
#include "lua_tts.h"
#include "lua_translate.h"
#include "utils.h"

#include <string>

// ---------------------------------------------------------------------------
// HoundTTS.init(writedir)
// Called once after require("HoundTTS") to set the Saved Games path and
// load credentials from Config\HoundTTS-credentials.ini.
// ---------------------------------------------------------------------------
static int l_init(lua_State* L) {
    const char* writedir = luaL_checkstring(L, 1);
    HoundTTS::ConfigReader::Instance().Load(std::string(writedir));
    HoundTTS::Logger::Instance().Init(std::string(writedir));
    std::string lvl = HoundTTS::ConfigReader::Instance().GetLogLevel();
    HoundTTS::LogLevel logLvl = HoundTTS::LogLevel::LEVEL_ERROR;
    if (lvl == "debug" || lvl == "DEBUG") logLvl = HoundTTS::LogLevel::LEVEL_DEBUG;
    else if (lvl == "info" || lvl == "INFO") logLvl = HoundTTS::LogLevel::LEVEL_INFO;
    HoundTTS::Logger::Instance().SetLevel(logLvl);
    return 0;
}

// ---------------------------------------------------------------------------
// Module function table
// ---------------------------------------------------------------------------
static const luaL_Reg HoundTTS_funcs[] = {
    { "init",                 l_init                },
    { "textToSpeech",         l_textToSpeech        },
    { "getSpeechTime",        l_getSpeechTime       },
    { "startNoise",           l_startNoise          },
    { "startTone",            l_startTone           },
    { "updateSession",        l_updateSession       },
    { "killAllSessions",      l_killAllSessions     },
    { "translateAsync",       l_translateAsync      },
    { "getTranslationResult", l_getTranslationResult},
    { "clearPCMCache",        l_clearPCMCache       },
    { "getCacheStats",        l_getCacheStats       },
    { nullptr, nullptr }
};

// ---------------------------------------------------------------------------
// DLL entry point for Lua: require("HoundTTS")
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int luaopen_HoundTTS(lua_State* L) {
    luaL_register(L, "HoundTTS", HoundTTS_funcs);
    lua_pushstring(L, HOUNDTTS_VERSION);
    lua_setfield(L, -2, "version");
    return 1;
}
