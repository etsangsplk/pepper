/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010-2011 Jonas Gehring
 *
 * Released under the GNU General Public License, version 3.
 * Please see the COPYING file in the source distribution for license
 * terms and conditions, or see http://www.gnu.org/licenses/.
 *
 * file: subversion.h
 * Subversion repository backend (interface)
 */


#ifndef SUBVERSION_BACKEND_H_
#define SUBVERSION_BACKEND_H_


#include "backend.h"

class SvnConnection;
class SvnDiffstatPrefetcher;


class SubversionBackend : public Backend
{
	public:
		class SvnLogIterator : public LogIterator
		{
			public:
				SvnLogIterator(SvnConnection *connection, const std::string &prefix, int64_t startrev, int64_t endrev);
				~SvnLogIterator();

				bool nextIds(std::queue<std::string> *queue);

			protected:
				void run();

			private:
				SvnConnection *d;
				std::string m_prefix;
				int64_t m_startrev, m_endrev;
				sys::parallel::Mutex m_mutex;
				sys::parallel::WaitCondition m_cond;
				std::vector<std::string>::size_type m_index;
				bool m_finished;
		};

	public:
		SubversionBackend(const Options &options);
		~SubversionBackend();

		void init();

		std::string name() const { return "subversion"; }
		static bool handles(const std::string &url);

		std::string uuid();
		std::string head(const std::string &branch = std::string());
		std::string mainBranch();
		std::vector<std::string> branches();
		std::vector<Tag> tags();
		Diffstat diffstat(const std::string &id);
		void filterDiffstat(Diffstat *stat);
		std::vector<std::string> tree(const std::string &id = std::string());

		LogIterator *iterator(const std::string &branch = std::string(), int64_t start = -1, int64_t end = -1);
		void prefetch(const std::vector<std::string> &ids);
		Revision *revision(const std::string &id);
		void finalize();

		void printHelp() const;

	private:
		SvnConnection *d;
		SvnDiffstatPrefetcher *m_prefetcher;
};


#endif // SUBVERSION_BACKEND_H_
