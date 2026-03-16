#pragma once

#include "lua/lua.hpp"
#include <string>

// Shared helpers for reading fields from a Lua table on the stack.

inline std::string GetTableString(lua_State* L, int tableIdx, const char* field, const char* def = "") {
    lua_getfield(L, tableIdx, field);
    const char* val = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    std::string result(val);
    lua_pop(L, 1);
    return result;
}

inline double GetTableNumber(lua_State* L, int tableIdx, const char* field, double def = 0.0) {
    lua_getfield(L, tableIdx, field);
    double val = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return val;
}

inline int GetTableInt(lua_State* L, int tableIdx, const char* field, int def = 0) {
    lua_getfield(L, tableIdx, field);
    int val = lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : def;
    lua_pop(L, 1);
    return val;
}

inline bool GetTableBool(lua_State* L, int tableIdx, const char* field, bool def = false) {
    lua_getfield(L, tableIdx, field);
    bool val = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : def;
    lua_pop(L, 1);
    return val;
}
