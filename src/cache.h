/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010-2011 Jonas Gehring
 *
 * Released under the GNU General Public License, version 3.
 * Please see the COPYING file in the source distribution for license
 * terms and conditions, or see http://www.gnu.org/licenses/.
 *
 * file: cache.h
 * Revision cache (interface)
 */


#ifndef CACHE_H_
#define CACHE_H_


#include "backend.h"

class BIStream;
class BOStream;
class Revision;


// This cache should be transparent and inherits the wrapped class
class Cache : public Backend
{
	private:
		typedef enum {
			Ok,
			Abort,
			Clear,
			UnknownVersion
		} VersionCheckResult;

	public:
		Cache(Backend *backend, const Options &options);
		~Cache();

		void init() { }
		void open() { m_backend->open(); }
		void close() { flush(); m_backend->close(); }

		std::string name() const { return m_backend->name(); }

		std::string uuid() { if (m_uuid.empty()) m_uuid = m_backend->uuid(); return m_uuid; }

		std::string head(const std::string &branch = std::string()) { return m_backend->head(branch); }
		std::string mainBranch() { return m_backend->mainBranch(); }
		std::vector<std::string> branches() { return m_backend->branches(); }
		std::vector<Tag> tags() { return m_backend->tags(); }
		Diffstat diffstat(const std::string &id);
		void filterDiffstat(Diffstat *stat) { m_backend->filterDiffstat(stat); }
		std::vector<std::string> tree(const std::string &id = std::string()) { return m_backend->tree(id); }
		std::string cat(const std::string &path, const std::string &id = std::string()) { return m_backend->cat(path, id); };

		LogIterator *iterator(const std::string &branch = std::string(), int64_t start = -1, int64_t end = -1) { return m_backend->iterator(branch, start, end); }
		void prefetch(const std::vector<std::string> &ids);
		Revision *revision(const std::string &id);
		void finalize() { m_backend->finalize(); }

		static std::string cacheFile(Backend *backend, const std::string &name);

		void flush();
		void check();

	private:
		bool lookup(const std::string &id);
		void put(const std::string &id, const Revision &rev);
		Revision *get(const std::string &id);
		void load();
		void clear();
		VersionCheckResult checkVersion(int version);

		static void checkDir(const std::string &path, bool *created = NULL);

	private:
		Backend *m_backend;
		std::string m_uuid; // Cached backend UUID
		BOStream *m_iout, *m_cout;
		BIStream *m_cin;
		uint32_t m_coindex, m_ciindex;
		bool m_loaded;

		std::map<std::string, std::pair<uint32_t, uint32_t> > m_index;
};


#endif // CACHE_H_
