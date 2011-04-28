/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010-2011 Jonas Gehring
 *
 * Released under the GNU General Public License, version 3.
 * Please see the COPYING file in the source distribution for license
 * terms and conditions, or see http://www.gnu.org/licenses/.
 *
 * file: git.cpp
 * Git repository backend
 */


#include "main.h"

#include <algorithm>
#include <fstream>

#include "jobqueue.h"
#include "logger.h"
#include "options.h"
#include "revision.h"
#include "utils.h"

#include "syslib/datetime.h"
#include "syslib/fs.h"
#include "syslib/io.h"
#include "syslib/parallel.h"

#include "backends/git.h"


// Diffstat fetching worker thread, using a pipe to write data to "git diff-tree"
class GitDiffstatPipe : public sys::parallel::Thread
{
public:
	GitDiffstatPipe(const std::string &git, JobQueue<std::string, Diffstat> *queue)
		: m_git(git), m_queue(queue)
	{
	}

	static Diffstat diffstat(const std::string &git, const std::string &id, const std::string &parent = std::string())
	{
		if (!parent.empty()) {
			sys::io::PopenStreambuf buf(git.c_str(), "diff-tree", "-U0", "--no-renames", parent.c_str(), id.c_str());
			std::istream in(&buf);
			Diffstat stat = DiffParser::parse(in);
			if (buf.close() != 0) {
				throw PEX("git diff-tree command failed");
			}
			return stat;
		} else {
			sys::io::PopenStreambuf buf(git.c_str(), "diff-tree", "-U0", "--no-renames", "--root", id.c_str());
			std::istream in(&buf);
			Diffstat stat = DiffParser::parse(in);
			if (buf.close() != 0) {
				throw PEX("git diff-tree command failed");
			}
			return stat;
		}
	}

protected:
	void run()
	{
		// TODO: Error checking
		sys::io::PopenStreambuf buf(m_git.c_str(), "diff-tree", "-U0", "--no-renames", "--stdin", "--root", NULL, NULL, std::ios::in | std::ios::out);
		std::istream in(&buf);
		std::ostream out(&buf);

		std::string revision;
		while (m_queue->getArg(&revision)) {
			std::vector<std::string> revs = utils::split(revision, ":");

			if (revs.size() < 2) {
				out << revs[0] << '\n';
			} else {
				out << revs[1] << " " << revs[0] << '\n';
			}

			// We use EOF characters to mark the end of a revision for
			// the diff parser. git diff-tree won't understand this line
			// and simply write the EOF.
			out << (char)EOF << '\n' << std::flush;

			Diffstat stat = DiffParser::parse(in);
			m_queue->done(revision, stat);
		}
	}

private:
	std::string m_git;
	JobQueue<std::string, Diffstat> *m_queue;
};


// Meta-data fetching worker thread, using a pipe to write data to "git rev-list"
class GitMetaDataPipe : public sys::parallel::Thread
{
public:
	struct Data
	{
		int64_t date;
		std::string author;
		std::string message;
	};

public:
	GitMetaDataPipe(const std::string &git, JobQueue<std::string, Data> *queue)
		: m_git(git), m_queue(queue)
	{
	}

	static void parseHeader(const std::vector<std::string> &header, Data *dest)
	{
		// TODO: Proper exception descriptions
		if (header.size() < 6) {
			throw PEX(utils::strprintf("Unable to parse meta-data"));
		}

		// Parse author information
		unsigned int i = 0;
		while (i < header.size() && header[i].compare(0, 7, "author ")) {
			++i;
		}
		if (i >= header.size()) {
			throw PEX(utils::strprintf("Unable to parse author information"));
		}
		std::vector<std::string> authorln = utils::split(header[i], " ");
		if (authorln.size() < 4) {
			throw PEX(utils::strprintf("Unable to parse author information"));
		}

		// Author: 2nd to n-2nd entry
		std::string author = utils::join(authorln.begin()+1, authorln.end()-2, " ");
		// Strip email address, assuming a start at the last "<" (not really compliant with RFC2882)
		dest->author = utils::trim(author.substr(0, author.find_last_of('<')));

		// Committer date: last 2 entries in the form %s %z
		while (i < header.size() && header[i].compare(0, 10, "committer ")) {
			++i;
		}
		if (i >= header.size()) {
			throw PEX(utils::strprintf("Unable to parse date information"));
		}
		std::vector<std::string> dateln = utils::split(header[i], " ");
		if (dateln.size() < 2) {
			throw PEX(utils::strprintf("Unable to parse date information"));
		}
		int64_t off = 0;
		if (!utils::str2int(dateln[dateln.size()-2], &(dest->date), 10) || !utils::str2int(dateln[dateln.size()-1], &off, 10)) {
			throw PEX(utils::strprintf("Unable to parse date information"));
		}
		dest->date += off;

		// Last but not least: commit message
		while (i < header.size() && !header[i].empty()) {
			++i;
		}
		dest->message.clear();
		++i;
		while (i < header.size()) {
			if (header[i].length() > 4) {
				dest->message += header[i].substr(4);
			}
			if (i < header.size()-1) {
				dest->message += "\n";
			}
			++i;
		}
	}

protected:
	void run()
	{
		Data data;
		size_t maxrevs = 128;
		std::string revision;
		std::vector<std::string> revisions;
		std::map<std::string, std::string> revmap;
		while (m_queue->getArgs(&revisions, maxrevs)) {
			// TODO: Doesn't work with git < 1.7
			// TODO: Error checking
			sys::io::PopenStreambuf buf(m_git.c_str(), "rev-list", "--stdin", "--header", "--no-walk", NULL, NULL, NULL, std::ios::in | std::ios::out);
			std::istream in(&buf);
			std::ostream out(&buf);

			revmap.clear();
			for (size_t i = 0; i < revisions.size(); i++) {
				std::string rev = utils::split(revisions[i], ":").back();
				out << rev << '\n';
				revmap[rev] = revisions[i];
			}
			buf.closeWrite();

			// Parse single headers
			std::string str;
			std::vector<std::string> header;
			while (in.good()) {
				std::getline(in, str);
				if (str.size() > 0 && str[0] == '\0') {
					if (revmap.find(header[0]) != revmap.end()) {
						try {
							std::cout << "DONE: " << header[0] << std::endl;
							parseHeader(header, &data);
							m_queue->done(revmap[header[0]], data);
						} catch (const std::exception &ex) {
							Logger::info() << "Error parsing revision header: " << ex.what() << endl;
							m_queue->failed(revmap[header[0]]);
						}
					}

					header.clear();
					header.push_back(str.substr(1));
				} else {
					header.push_back(str);
				}
			}
		}
	}

private:
	std::string m_git;
	JobQueue<std::string, Data> *m_queue;
};


// Handles the prefetching of revision meta-data and diffstats
class GitRevisionPrefetcher
{
public:
	GitRevisionPrefetcher(const std::string &git, int n = -1)
		: m_metaQueue(4096)
	{
		if (n < 0) {
			n = std::max(1, sys::parallel::idealThreadCount() / 2);
		}
		Logger::info() << "GitBackend: Using " << n << " threads for prefetching diffstats" << endl;
		for (int i = 0; i < n; i++) {
			sys::parallel::Thread *thread = new GitDiffstatPipe(git, &m_diffQueue);
			thread->start();
			m_threads.push_back(thread);
		}

		Logger::info() << "GitBackend: Using " << n << " threads for prefetching meta-data" << endl;
		for (int i = 0; i < n; i++) {
			sys::parallel::Thread *thread = new GitMetaDataPipe(git, &m_metaQueue);
			thread->start();
			m_threads.push_back(thread);
		}
	}

	~GitRevisionPrefetcher()
	{
		for (unsigned int i = 0; i < m_threads.size(); i++) {
			delete m_threads[i];
		}
	}

	void stop()
	{
		m_diffQueue.stop();
		m_metaQueue.stop();
	}

	void wait()
	{
		for (unsigned int i = 0; i < m_threads.size(); i++) {
			m_threads[i]->wait();
		}
	}

	void prefetch(const std::vector<std::string> &revisions)
	{
		m_diffQueue.put(revisions);
		m_metaQueue.put(revisions);
	}

	bool getDiffstat(const std::string &revision, Diffstat *dest)
	{
		return m_diffQueue.getResult(revision, dest);
	}

	bool getMeta(const std::string &revision, GitMetaDataPipe::Data *dest)
	{
		return m_metaQueue.getResult(revision, dest);
	}

	bool willFetchDiffstat(const std::string &revision)
	{
		return m_diffQueue.hasArg(revision);
	}

	bool willFetchMeta(const std::string &revision)
	{
		return m_metaQueue.hasArg(revision);
	}

private:
	JobQueue<std::string, Diffstat> m_diffQueue;
	JobQueue<std::string, GitMetaDataPipe::Data> m_metaQueue;
	std::vector<sys::parallel::Thread *> m_threads;
};


// Constructor
GitBackend::GitBackend(const Options &options)
	: Backend(options), m_prefetcher(NULL)
{

}

// Destructor
GitBackend::~GitBackend()
{
	if (m_prefetcher) {
		m_prefetcher->stop();
		m_prefetcher->wait();
		delete m_prefetcher;
	}
}

// Initializes the backend
void GitBackend::init()
{
	std::string repo = m_opts.repository();
	if (sys::fs::exists(repo + "/HEAD")) {
		setenv("GIT_DIR", repo.c_str(), 1);
	} else if (sys::fs::exists(repo + "/.git/HEAD")) {
		setenv("GIT_DIR", (repo + "/.git").c_str(), 1);
	} else if (sys::fs::fileExists(repo + "/.git")) {
		PDEBUG << "Parsing .git file" << endl;
		std::ifstream in((repo + "/.git").c_str(), std::ios::in);
		if (!in.good()) {
			throw PEX(utils::strprintf("Unable to read from .git file: %s", repo.c_str()));
		}
		std::string str;
		std::getline(in, str);
		std::vector<std::string> parts = utils::split(str, ":");
		if (parts.size() < 2) {
			throw PEX(utils::strprintf("Unable to parse contents of .git file: %s", str.c_str()));
		}
		setenv("GIT_DIR", utils::trim(parts[1]).c_str(), 1);
	} else {
		throw PEX(utils::strprintf("Not a git repository: %s", repo.c_str()));
	}

	// Search for git executable
	char *path = getenv("PATH");
	if (path == NULL) {
		throw PEX("PATH is not set");
	}
	std::vector<std::string> ls;
#ifdef POS_WIN
	ls = utils::split(path, ";");
#else
	ls = utils::split(path, ":");
#endif
	for (size_t i = 0; i < ls.size(); i++) {
		std::string t = ls[i] + "/git";
#ifdef POS_WIN
		t += ".exe";
#endif
		if (sys::fs::fileExecutable(t)) {
			m_git = t;
		}
	}

	if (m_git.empty()) {
		throw PEX("Can't find git in PATH");
	}

	PDEBUG << "git executable is " << m_git << endl;
	PDEBUG << "GIT_DIR has been set to " << getenv("GIT_DIR") << endl;
}

// Returns true if this backend is able to access the given repository
bool GitBackend::handles(const std::string &url)
{
	if (sys::fs::dirExists(url+"/.git")) {
		return true;
	} else if (sys::fs::fileExists(url+"/.git")) {
		PDEBUG << "Detached repository detected" << endl;
		return true;
	} else if (sys::fs::dirExists(url) && sys::fs::fileExists(url+"/HEAD") && sys::fs::dirExists(url+"/objects")) {
		PDEBUG << "Bare repository detected" << endl;
		return true;
	}
	return false;
}

// Returns a unique identifier for this repository
std::string GitBackend::uuid()
{
	// Determine current main branch and the HEAD revision
	std::string branch = mainBranch();
	std::string headrev = head(branch);
	std::string oldroot, oldhead;
	int ret;

	// The $GIT_DIR/pepper.cache file caches branch names and their root
	// commits. It consists of lines of the form
	// $BRANCH_NAME $HEAD $ROOT
	std::string cachefile = std::string(getenv("GIT_DIR")) + "/pepper.cache";
	{
		std::ifstream in((std::string(getenv("GIT_DIR")) + "/pepper.cache").c_str());
		while (in.good()) {
			std::string str;
			std::getline(in, str);
			if (str.compare(0, branch.length(), branch)) {
				continue;
			}

			std::vector<std::string> parts = utils::split(str, " ");
			if (parts.size() == 3) {
				oldhead = parts[1];
				oldroot = parts[2];
				if (oldhead == headrev) {
					PDEBUG << "Found cached root commit" << endl;
					return oldroot;
				}
			}
			break;
		}
	}

	// Check if the old root commit is still valid by checking if the old head revision
	// is an ancestory of the current one
	std::string root;
	if (!oldroot.empty()) {
		std::string ref = sys::io::exec(&ret, m_git.c_str(), "rev-list", "-1", (oldhead + ".." + headrev).c_str());
		if (ret == 0 && !ref.empty()) {
			PDEBUG << "Old head " << oldhead << " is a valid ancestor, updating cached head" << endl;
			root = oldroot;
		}
	}

	// Get ID of first commit of the selected branch
	// Unfortunatley, the --max-count=n option results in n revisions counting from the HEAD.
	// This way, we'll always get the HEAD revision with --max-count=1.
	if (root.empty()) {
		sys::datetime::Watch watch;

		std::string id = sys::io::exec(&ret, m_git.c_str(), "rev-list", "--reverse", branch.c_str(), "--");
		if (ret != 0) {
			throw PEX(utils::strprintf("Unable to determine the root commit for branch '%s' (%d)", branch.c_str(), ret));
		}
		size_t pos = id.find_first_of('\n');
		if (pos == std::string::npos) {
			throw PEX(utils::strprintf("Unable to determine the root commit for branch '%s' (%d)", branch.c_str(), ret));
		}

		root = id.substr(0, pos);
		PDEBUG << "Determined root commit in " << watch.elapsedMSecs() << " ms" << endl;
	}

	// Update the cache file
	std::string newfile = cachefile + ".tmp";
	FILE *out = fopen(newfile.c_str(), "w");
	if (out == NULL) {
		throw PEX_ERRNO();
	}
	fprintf(out, "%s %s %s\n", branch.c_str(), headrev.c_str(), root.c_str());
	{
		std::ifstream in(cachefile.c_str());
		while (in.good()) {
			std::string str;
			std::getline(in, str);
			if (str.empty() && str.compare(0, branch.length(), branch)) {
				continue;
			}
			fprintf(out, "%s\n", str.c_str());
		}
		fsync(fileno(out));
		fclose(out);
	}
	sys::fs::rename(newfile, cachefile);

	return root;
}

// Returns the HEAD revision for the given branch
std::string GitBackend::head(const std::string &branch)
{
	int ret;
	std::string out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "-1", (branch.empty() ? "HEAD" : branch).c_str(), "--");
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve head commit for branch %s (%d)", branch.c_str(), ret));
	}
	return utils::trim(out);
}

// Returns the currently checked out branch
std::string GitBackend::mainBranch()
{
	int ret;
	std::string out = sys::io::exec(&ret, m_git.c_str(), "branch");
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve the list of branches (%d)", ret));
	}
	std::vector<std::string> branches = utils::split(out, "\n");
	for (unsigned int i = 0; i < branches.size(); i++) {
		if (branches[i].empty()) {
			continue;
		}
		if (branches[i][0] == '*') {
			return branches[i].substr(2);
		}
		branches[i] = branches[i].substr(2);
	}

	if (std::search_n(branches.begin(), branches.end(), 1, "master") != branches.end()) {
		return "master";
	} else if (std::search_n(branches.begin(), branches.end(), 1, "remotes/origin/master") != branches.end()) {
		return "remotes/origin/master";
	}

	// Fallback
	return "master";
}

// Returns a list of available local branches
std::vector<std::string> GitBackend::branches()
{
	int ret;
	std::string out = sys::io::exec(&ret, m_git.c_str(), "branch");
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve the list of branches (%d)", ret));
	}
	std::vector<std::string> branches = utils::split(out, "\n");
	for (unsigned int i = 0; i < branches.size(); i++) {
		if (branches[i].empty()) {
			branches.erase(branches.begin()+i);
			--i;
			continue;
		}
		branches[i] = branches[i].substr(2);
	}
	return branches;
}

// Returns a list of available tags
std::vector<Tag> GitBackend::tags()
{
	int ret;

	// Fetch list of tag names
	std::string out = sys::io::exec(&ret, m_git.c_str(), "tag");
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve the list of tags (%d)", ret));
	}
	std::vector<std::string> names = utils::split(out, "\n");
	std::vector<Tag> tags;

	// Determine corresponding commits
	for (unsigned int i = 0; i < names.size(); i++) {
		if (names[i].empty()) {
			continue;
		}

		std::string out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "-1", names[i].c_str());
		if (ret != 0) {
			throw PEX(utils::strprintf("Unable to retrieve the list of tags (%d)", ret));
		}

		std::string id = utils::trim(out);
		if (!id.empty()) {
			tags.push_back(Tag(id, names[i]));
		}
	}
	return tags;
}

// Returns a diffstat for the specified revision
Diffstat GitBackend::diffstat(const std::string &id)
{
	// Maybe it's prefetched
	if (m_prefetcher && m_prefetcher->willFetchDiffstat(id)) {
		Diffstat stat;
		if (!m_prefetcher->getDiffstat(id, &stat)) {
			throw PEX(utils::strprintf("Failed to retrieve diffstat for revision %s", id.c_str()));
		}
		return stat;
	}

	PDEBUG << "Fetching revision " << id << " manually" << endl;

	std::vector<std::string> revs = utils::split(id, ":");
	if (revs.size() > 1) {
		return GitDiffstatPipe::diffstat(m_git, revs[1], revs[0]);
	}
	return GitDiffstatPipe::diffstat(m_git, revs[0]);
}

// Returns a file listing for the given revision (defaults to HEAD)
std::vector<std::string> GitBackend::tree(const std::string &id)
{
	int ret;
	std::string out = sys::io::exec(&ret, m_git.c_str(), "ls-tree", "-r", "--full-name", "--name-only", (id.empty() ? "HEAD" : id.c_str()));
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve tree listing for ID '%s' (%d)", id.c_str(), ret));
	}
	std::vector<std::string> contents = utils::split(out, "\n");
	while (!contents.empty() && contents[contents.size()-1].empty()) {
		contents.pop_back();
	}
	return contents;
}

// Returns a revision iterator for the given branch
Backend::LogIterator *GitBackend::iterator(const std::string &branch, int64_t start, int64_t end)
{
	int ret;
	std::string out;
	if (start >= 0) {
		std::string maxage = utils::strprintf("--max-age=%lld", start);
		if (end >= 0) {
			std::string minage = utils::strprintf("--min-age=%lld", end);
			out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "--first-parent", "--reverse", maxage.c_str(), minage.c_str(), branch.c_str(), "--");
		} else {
			out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "--first-parent", "--reverse", maxage.c_str(), branch.c_str(), "--");
		}
	} else {
		if (end >= 0) {
			std::string minage = utils::strprintf("--min-age=%lld", end);
			out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "--first-parent", "--reverse", minage.c_str(), branch.c_str(), "--");
		} else {
			out = sys::io::exec(&ret, m_git.c_str(), "rev-list", "--first-parent", "--reverse", branch.c_str(), "--");
		}
	}
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve log for branch '%s' (%d)", branch.c_str(), ret));
	}
	std::vector<std::string> revisions = utils::split(out, "\n");
	while (!revisions.empty() && revisions[revisions.size()-1].empty()) {
		revisions.pop_back();
	}

	// Add parent revisions, so diffstat fetching will give correct results
	for (ssize_t i = revisions.size()-1; i > 0; i--) {
		revisions[i] = revisions[i-1] + ":" + revisions[i];
	}

	return new LogIterator(revisions);
}

// Starts prefetching the given revision IDs
void GitBackend::prefetch(const std::vector<std::string> &ids)
{
	if (m_prefetcher == NULL) {
		m_prefetcher = new GitRevisionPrefetcher(m_git);
	}
	m_prefetcher->prefetch(ids);
	PDEBUG << "Started prefetching " << ids.size() << " revisions" << endl;
}

// Returns the revision data for the given ID
Revision *GitBackend::revision(const std::string &id)
{
	// Unfortunately, older git versions don't have the %B format specifier
	// for unwrapped subject and body, so the raw commit headers will be parsed instead.
#if 0
	int ret;
	std::string meta = sys::io::exec(&ret, m_git.c_str(), "log", "-1", "--pretty=format:%ct\n%aN\n%B", id.c_str());
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve meta-data for revision '%s' (%d, %s)", id.c_str(), ret, meta.c_str()));
	}
	std::vector<std::string> lines = utils::split(meta, "\n");
	int64_t date = 0;
	std::string author;
	if (!lines.empty()) {
		utils::str2int(lines[0], &date);
		lines.erase(lines.begin());
	}
	if (!lines.empty()) {
		author = lines[0];
		lines.erase(lines.begin());
	}
	std::string msg = utils::join(lines, "\n");
	return new Revision(id, date, author, msg, diffstat(id));
#else

	// Maybe it's prefetched
	if (m_prefetcher && m_prefetcher->willFetchMeta(id)) {
		GitMetaDataPipe::Data data;
		if (!m_prefetcher->getMeta(id, &data)) {
			throw PEX(utils::strprintf("Failed to retrieve meta-data for revision %s", id.c_str()));
		}
		return new Revision(id, data.date, data.author, data.message, diffstat(id));
	}

	std::string rev = utils::split(id, ":").back();

	int ret;
	std::string header = sys::io::exec(&ret, m_git.c_str(), "rev-list", "-1", "--header", rev.c_str());
	if (ret != 0) {
		throw PEX(utils::strprintf("Unable to retrieve meta-data for revision '%s' (%d, %s)", rev.c_str(), ret, header.c_str()));
	}
	std::vector<std::string> lines = utils::split(header, "\n");
	GitMetaDataPipe::Data data;
	GitMetaDataPipe::parseHeader(lines, &data);
	return new Revision(id, data.date, data.author, data.message, diffstat(id));
#endif
}

// Handle cleanup of diffstat scheduler
void GitBackend::finalize()
{
	if (m_prefetcher) {
		PDEBUG << "Waiting for prefetcher... " << endl;
		m_prefetcher->stop();
		m_prefetcher->wait();
		delete m_prefetcher;
		m_prefetcher = NULL;
		PDEBUG << "done" << endl;
	}
}
