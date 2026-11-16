#pragma once

#include "lua/lua.hpp"

// Lua-callable async translation functions registered in luaopen_HoundTTS.
// C linkage so the declarations match lua_CFunction exactly.
extern "C" {
int l_translateAsync(lua_State* L);
int l_getTranslationResult(lua_State* L);
}
