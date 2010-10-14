/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: luahelpersq.h
 * Helper functions for interacting with the Lua API
 */


#ifndef LUAHELPERS_H_
#define LUAHELPERS_H_


#include <string>
#include <vector>

#include "lunar.h"


namespace LuaHelpers
{

inline int push(lua_State *L, int i) {
	lua_pushinteger(L, i);
	return 1;
}

inline int push(lua_State *L, double i) {
	lua_pushnumber(L, i);
	return 1;
}

inline int push(lua_State *L, bool i) {
	lua_pushboolean(L, (int)i);
	return 1;
}

inline int push(lua_State *L, const char *s) {
	lua_pushstring(L, s);
	return 1;
}

inline int push(lua_State *L, const std::string &s) {
	lua_pushstring(L, s.c_str());
	return 1;
}

inline int push(lua_State *L, const std::vector<std::string> &v) {
	lua_createtable(L, v.size(), 0);
	int table = lua_gettop(L);
	for (unsigned int i = 0; i < v.size(); i++) {
		lua_pushstring(L, v[i].c_str());
		lua_rawseti(L, table, i+1);
	}
	return 1;
}

inline int pushError(lua_State *L, const std::string &e) {
	return luaL_error(L, e.c_str());
}

template<typename T1>
inline void call(lua_State *L, const T1 &arg1, int nresults) {
	push(L, arg1);
	lua_call(L, 1, nresults);
}

template<typename T1, typename T2>
inline void call(lua_State *L, const T1 &arg1, const T2 &arg2, int nresults) {
	push(L, arg1);
	push(L, arg2);
	lua_call(L, 2, nresults);
}

template<typename T1, typename T2, typename T3>
inline void call(lua_State *L, const T1 &arg1, const T2 &arg2, const T3 &arg3, int nresults) {
	push(L, arg1);
	push(L, arg2);
	push(L, arg3);
	lua_call(L, 3, nresults);
}

} // namespace LuaHelpers


#endif // LUAHELPERS_H_
