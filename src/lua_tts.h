#pragma once

#include "lua/lua.hpp"

// Lua-callable TTS functions registered in luaopen_HoundTTS.
int l_textToSpeech(lua_State* L);
int l_getSpeechTime(lua_State* L);
