#pragma once

#include "lua/lua.hpp"

// Lua-callable async translation functions registered in luaopen_HoundTTS.
int l_translateAsync(lua_State* L);
int l_getTranslationResult(lua_State* L);
