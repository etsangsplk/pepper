/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: report.cpp
 * Lua interface for gathering repository data
 */


#include <cstdlib>
#include <iostream>

#include "backend.h"
#include "diffstat.h"
#include "globals.h"
#include "repository.h"
#include "revision.h"

#include "luadiffstat.h"
#include "luahelpers.h"
#include "luarepository.h"
#include "luarevision.h"
#include "plot.h"
#include "report.h"


namespace Report
{

// Global variables
static Repository *repo;
static LuaRepository *luarepo;


// Returns the current repository
static int repository(lua_State *L)
{
	return LuaHelpers::push(L, luarepo);
}

// Maps a lua function on all revisions of a given branch
static int map_branch(lua_State *L)
{
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "Invalid number of arguments (2 expected)");
	}

	luaL_checktype(L, -2, LUA_TFUNCTION);
	std::string branch = LuaHelpers::check(L);
	lua_pop(L, 1);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);

	bool verbose = true;
	if (verbose) {
		std::cerr << "Initializing iterator... " << std::flush;
	}

	Backend *backend = repo->backend();
	Backend::RevisionIterator *it;
	try {
		it = backend->iterator(branch);
		backend->prepare(it);
	} catch (const Pepper::Exception &ex) {
		if (verbose) {
			std::cerr << "failed" << std::endl;
		}
		return LuaHelpers::pushError(L, ex.what(), ex.where());
	}

	if (verbose) {
		std::cerr << "done" << std::endl;
		std::cerr << "Mapping revisions... " << std::flush;
	}

	while (!it->atEnd()) {
		Revision *revision = NULL;
		try {
			revision = backend->revision(it->next());
		} catch (const Pepper::Exception &ex) {
			return LuaHelpers::pushError(L, ex.what(), ex.where());
		}
		LuaRevision luarev(revision);

		lua_rawgeti(L, LUA_REGISTRYINDEX, callback);
		LuaHelpers::push(L, &luarev);
		lua_call(L, 1, 1);
		lua_pop(L, 1);

		if (verbose) {
			std::cerr << "\r\e[0K";
			std::cerr << "Mapping revisions... " << revision->id() << std::flush;
		}

		if (Globals::terminate) {
			return LuaHelpers::pushError(L, "Terminated");
		}

		delete revision;
	}

	if (verbose) {
		std::cerr << "\r\e[0K";
		std::cerr << "Mapping revisions... done" << std::endl;
	}

	try {
		backend->finalize();
	} catch (const Pepper::Exception &ex) {
		return LuaHelpers::pushError(L, ex.what(), ex.where());
	}
	delete it;
	return 0;
}

// Function table of the report library
static const struct luaL_reg report[] = {
	{"repository", repository},
	{"map_branch", map_branch},
	{NULL, NULL}
};


// Runs a scripted report using the given backend
int run(const char *script, Backend *backend)
{
	// Setup lua context
	lua_State *L = lua_open();
	luaL_openlibs(L);

	// Register report functions
	luaL_register(L, "pepper.report", report);

	// Register binding classes
	Lunar<LuaRepository>::Register(L, "pepper");
	Lunar<LuaRevision>::Register(L, "pepper");
	Lunar<LuaDiffstat>::Register(L, "pepper");
	Lunar<Plot>::Register(L, "pepper");

	// Set the current repository
	Report::repo = new Repository(backend);
	Report::luarepo = new LuaRepository(repo);

	// Run the script
	int ret = EXIT_SUCCESS;
	if (luaL_dofile(L, script) != 0) {
		std::cerr << "Error running report: " << lua_tostring(L, -1) << std::endl;
		ret = EXIT_FAILURE;
	}

	if (Globals::terminate) {
		ret = EXIT_FAILURE;
	}

	// Clean up
	lua_gc(L, LUA_GCCOLLECT, 0);
	lua_close(L);
	delete Report::repo;
	delete Report::luarepo;
	return ret;
}

} // namespace Report
