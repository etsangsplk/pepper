/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: repository.cpp
 * Repository interface for report scripts
 */


#include "backend.h"
#include "luahelpers.h"
#include "options.h"

#include "repository.h"


// Constructor
Repository::Repository(Backend *backend)
	: m_backend(backend)
{

}

// Destructor
Repository::~Repository()
{

}

// Returns the backend
Backend *Repository::backend() const
{
	return m_backend;
}

/*
 * Lua binding
 */

const char Repository::className[] = "repository";
Lunar<Repository>::RegType Repository::methods[] = {
	LUNAR_DECLARE_METHOD(Repository, url),
	LUNAR_DECLARE_METHOD(Repository, type),
	LUNAR_DECLARE_METHOD(Repository, head),
	LUNAR_DECLARE_METHOD(Repository, main_branch),
	LUNAR_DECLARE_METHOD(Repository, branches),
	{0,0}
};

Repository::Repository(lua_State *) {
	m_backend = NULL;
}

int Repository::url(lua_State *L) {
	return (m_backend ? LuaHelpers::push(L, m_backend->options().repoUrl()) : LuaHelpers::pushNil(L));
}

int Repository::type(lua_State *L) {
	return (m_backend ? LuaHelpers::push(L, m_backend->name()) : LuaHelpers::pushNil(L));
}

int Repository::head(lua_State *L) {
	if (m_backend == NULL) return LuaHelpers::pushNil(L);
	std::string branch;
	if (lua_gettop(L) > 0) {
		branch = LuaHelpers::pops(L);
	}
	std::string h;
	try {
		h = m_backend->head(branch);
	} catch (const Pepper::Exception &ex) {
		return LuaHelpers::pushError(L, ex.what(), ex.what());
	}
	return LuaHelpers::push(L, h);
}

int Repository::main_branch(lua_State *L) {
	if (m_backend == NULL) return LuaHelpers::pushNil(L);
	return LuaHelpers::push(L, m_backend->mainBranch());
}

int Repository::branches(lua_State *L) {
	if (m_backend == NULL) return LuaHelpers::pushNil(L);
	std::vector<std::string> b;
	try {
		b = m_backend->branches();
	} catch (const Pepper::Exception &ex) {
		return LuaHelpers::pushError(L, ex.what(), ex.what());
	}
	return LuaHelpers::push(L, b);
}
