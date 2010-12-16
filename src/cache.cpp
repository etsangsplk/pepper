/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: cache.cpp
 * Revision cache
 */


#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

#include "main.h"

#include "bstream.h"
#include "diffstat.h"
#include "fs.h"
#include "globals.h"
#include "logger.h"
#include "options.h"
#include "revision.h"
#include "utils.h"

#include "cache.h"


#define MAX_CACHEFILE_SIZE 4194304


// Constructor
Cache::Cache(Backend *backend, const Options &options)
	: Backend(options), m_backend(backend), m_iout(NULL), m_cout(NULL),
	  m_cin(0), m_coindex(0), m_ciindex(0)
{
	load();
}

// Destructor
Cache::~Cache()
{
	delete m_backend;
	delete m_iout;
	delete m_cout;
	delete m_cin;
}

// Returns a diffstat for the specified revision
Diffstat Cache::diffstat(const std::string &id)
{
	if (!lookup(id)) {
		PTRACE << "Cache miss: " << id << endl;
		return m_backend->diffstat(id);
	}

	PTRACE << "Cache hit: " << id << endl;
	Revision *r = get(id);
	Diffstat stat = r->diffstat();
	delete r;
	return stat;
}

// Tells the wrapped backend to pre-fetch revisions that are not cached yet
void Cache::prefetch(const std::vector<std::string> &ids)
{
	std::vector<std::string> missing;
	for (unsigned int i = 0; i < ids.size(); i++) {
		if (!lookup(ids[i])) {
			missing.push_back(ids[i]);
		}
	}

	Logger::info() << "Cache: " << (ids.size() - missing.size()) << " of " << ids.size() << " revisions already cached, prefetching " << missing.size() << endl;
	m_backend->prefetch(missing);
}

// Returns the revision data for the given ID
Revision *Cache::revision(const std::string &id)
{
	if (!lookup(id)) {
		PTRACE << "Cache miss: " << id << endl;
		Revision *r = m_backend->revision(id);
		put(id, *r);
		return r;
	}

	PTRACE << "Cache hit: " << id << endl;
	return get(id);
}

// Flushes and closes the cache streams
void Cache::flush()
{
	delete m_iout;
	m_iout = NULL;
	delete m_cout;
	m_cout = NULL;
	delete m_cin;
	m_cin = NULL;
}

// Checks if the diffstat of the given revision is already cached
bool Cache::lookup(const std::string &id)
{
	return (m_index.find(id) != m_index.end());
}

// Adds the revision of the given revision to the cache
void Cache::put(const std::string &id, const Revision &rev)
{
	sys::parallel::MutexLocker locker(&Globals::cacheMutex);

	// Add revision to cache
	std::string dir = m_opts.cacheDir() + "/" + uuid(), path;
	if (m_cout == NULL) {
		m_coindex = 0;
		do {
			path = utils::strprintf("%s/cache.%u", dir.c_str(), m_coindex);
			// TODO: For compressed files, the stat returns the actual data size
			if (sys::fs::filesize(path) < MAX_CACHEFILE_SIZE) {
				break;
			}
			++m_coindex;
		} while (true);

		delete m_cout; m_cout = NULL;
		m_cout = new BOStream(path, true);
	} else if (m_cout->tell() >= MAX_CACHEFILE_SIZE) {
		delete m_cout;
		path = utils::strprintf("%s/cache.%u", dir.c_str(), ++m_coindex);
		m_cout = new BOStream(path, true);
	}

	uint32_t offset = m_cout->tell();
	MOStream rout;
	rev.write(rout);
	*m_cout << utils::compress(rout.data());

	// Add revision to index
	if (m_iout == NULL) {
		if (sys::fs::exists(dir + "/index")) {
			m_iout = new GZOStream(dir + "/index", true);
		} else {
			m_iout = new GZOStream(dir + "/index", false);
			// Version number
			*m_iout << (uint32_t)1;
		}
	}
	*m_iout << id;
	*m_iout << m_coindex << offset;
}

// Loads a revision from the cache
Revision *Cache::get(const std::string &id)
{
	std::string dir = m_opts.cacheDir() + "/" + uuid();
	std::pair<uint32_t, uint32_t> offset = m_index[id];
	std::string path = utils::strprintf("%s/cache.%u", dir.c_str(), offset.first);
	if (m_cin == NULL || offset.first != m_ciindex) {
		delete m_cout; m_cout = NULL;
		m_cin = new BIStream(path);
		m_ciindex = offset.first;
		if (!m_cin->ok()) {
			throw PEX(utils::strprintf("Unable to read from cache file: %s", path.c_str()));
		}
	}
	if (!m_cin->seek(offset.second)) {
		throw PEX(utils::strprintf("Unable to read from cache file: %s", path.c_str()));
	}

	Revision *rev = new Revision(id);
	std::vector<char> data;
	*m_cin >> data;
	data = utils::uncompress(data);
	if (data.empty()) {
		throw PEX(utils::strprintf("Unable to read from cache file: %s", path.c_str()));
	}
	MIStream rin(data);
	if (!rev->load(rin)) {
		throw PEX(utils::strprintf("Unable to read from cache file: %s", path.c_str()));
	}
	return rev;
}

// Loads the index file of the cache
void Cache::load()
{
	m_index.clear();

	std::string path = m_opts.cacheDir() + "/" + uuid();
	PDEBUG << "Using cache dir: " << path << endl;
	struct stat statbuf;
	if (stat(path.c_str(), &statbuf) == -1) {
		// Create the cache directory
		if (sys::fs::mkpath(path) < 0) {
			throw PEX(utils::strprintf("Unable to create cache directory: %s", path.c_str()));
		}
		Logger::info() << "Cache: Ceating cache directory for '" << uuid() << '\'' << endl;
		return;
	}

	GZIStream in(path+"/index");
	if (!in.ok()) {
		Logger::info() << "Cache: Empty cache for '" << uuid() << '\'' << endl;
		return;
	}

	uint32_t version;
	in >> version;
	if (version != 1) {
		throw PEX(utils::strprintf("Unkown cache version number %u", version));
	}

	std::string buffer;
	std::pair<uint32_t, uint32_t> pos;
	while (!in.eof()) {
		in >> buffer;
		if (buffer.empty()) {
			break;
		}
		in >> pos.first >> pos.second;
		m_index[buffer] = pos;
	}

	Logger::info() << "Cache: " << m_index.size() << " cached revisions for '" << uuid() << '\'' << endl;
}
