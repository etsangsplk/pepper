/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: subversion.cpp
 * Subversion repository backend
 */


#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>

#include <svn_client.h>
#include <svn_cmdline.h>
#include <svn_fs.h>
#include <svn_path.h>
#include <svn_pools.h>
#include <svn_ra.h>
#include <svn_time.h>
#include <svn_utf.h>

#include "main.h"
#include "options.h"
#include "revision.h"
#include "thread.h"
#include "utils.h"

#include "backends/subversion.h"
#include "backends/subversion_p.h"


// Subversion callback for log messages
static svn_error_t *logReceiver(void *baton, svn_log_entry_t *entry, apr_pool_t *pool) 
{
	std::vector<std::string> *dest = static_cast<std::vector<std::string> *>(baton);
	dest->push_back(utils::int2str(entry->revision));
	return SVN_NO_ERROR;
}

// Constructor
SubversionBackend::SubversionLogIterator::SubversionLogIterator(SvnConnection *c, const std::string &prefix, long int head)
	: Backend::LogIterator()
{
	// Determine revisions
	apr_pool_t *pool = svn_pool_create(c->pool);
	apr_array_header_t *path = apr_array_make(pool, 1, sizeof (const char *));
	APR_ARRAY_PUSH(path, const char *) = svn_path_canonicalize(prefix.empty() ? "." : prefix.c_str(), pool);
	apr_array_header_t *props = apr_array_make(pool, 1, sizeof (const char *)); // Intentionally empty

//	svn_error_t *err = svn_ra_get_log2(c->ra, path, 0, head, 0, FALSE, TRUE, FALSE, props, &logReceiver, &m_ids, pool);
	svn_error_t *err = svn_ra_get_log2(c->ra, path, 0, head, 0, FALSE, FALSE /* otherwise, copy history will be ignored */, FALSE, props, &logReceiver, &m_ids, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}

	svn_pool_clear(pool);
}


// Constructor
SubversionBackend::SubversionBackend(const Options &options)
	: Backend(options), d(new SvnConnection()), m_sched(NULL)
{

}

// Destructor
SubversionBackend::~SubversionBackend()
{
	delete d;
}

// Opens the connection to the repository
void SubversionBackend::init()
{
	// Initialize the Subversion C library
#ifndef WIN32
	if (svn_cmdline_init(PACKAGE, stderr) != EXIT_SUCCESS) {
		throw PEX("Failed to initialize Subversion library");
	}
	atexit(apr_terminate);
#else // !WIN32
	if (svn_cmdline_init(PACKAGE, NULL) != EXIT_SUCCESS) {
		throw PEX("Failed to initialize Subversion library");
	}
#endif // !WIN32

	std::string url = m_opts.repoUrl();
	if (!url.compare(0, 1, "/")) {
		url = std::string("file://") + url;
	}
	d->open(url, m_opts.authData());
}

// Returns a unique identifier for this repository
std::string SubversionBackend::uuid()
{
	apr_pool_t *pool = svn_pool_create(d->pool);
	const char *id;
	svn_error_t *err = svn_ra_get_uuid2(d->ra, &id, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}
	std::string sid(id);
	svn_pool_destroy(pool);
	return sid;
}

// Returns the HEAD revision for the current branch
std::string SubversionBackend::head(const std::string &branch)
{
	apr_pool_t *pool = svn_pool_create(d->pool);
	svn_revnum_t rev;
	svn_error_t *err = svn_ra_get_latest_revnum(d->ra, &rev, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}
	svn_pool_destroy(pool);
	return utils::int2str(rev);
}

// Returns the standard branch (i.e., trunk)
std::string SubversionBackend::mainBranch()
{
	return "trunk";
}

// Returns a list of available branches
std::vector<std::string> SubversionBackend::branches()
{
	// Assume the repository has a standard layout and branches can be found
	// in "/branches/", if any
	std::vector<std::string> branches;

	apr_pool_t *pool = svn_pool_create(d->pool);

	// Check if branches directoy is present
	svn_dirent_t *dirent;
	svn_error_t *err = svn_ra_stat(d->ra, "branches", SVN_INVALID_REVNUM, &dirent, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}

	if (dirent == NULL || dirent->kind != svn_node_dir) {
		// It's not here; let's call the main branch "trunk"
		branches.push_back("trunk");
		return branches;
	}

	// Get directory entries
	apr_hash_t *dirents = apr_hash_make(pool), *props = apr_hash_make(pool);
	err = svn_ra_get_dir2(d->ra, &dirents, NULL, &props, "branches", SVN_INVALID_REVNUM, SVN_DIRENT_KIND, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}

	branches.push_back("trunk");
	for (apr_hash_index_t *hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi)) {
		const char *entry;
		svn_dirent_t *dirent;
		apr_hash_this(hi, (const void **)(void *)&entry, NULL, (void **)(void *)&dirent);
		if (dirent->kind == svn_node_dir) {
			branches.push_back(std::string(entry));
		}
	}

	// Let's be nice
	std::sort(branches.begin()+1, branches.end());
	svn_pool_clear(pool);
	return branches;
}

// Returns a diffstat for the specified revision
Diffstat SubversionBackend::diffstat(const std::string &id)
{
	if (m_sched) {
		return m_sched->stat(id);
	}

	// First, produce diff of previous revision and this one and save it in a
	// temporary file
	apr_pool_t *pool = svn_pool_create(d->pool);

	svn_opt_revision_t rev1, rev2;
	rev1.kind = rev2.kind = svn_opt_revision_number;
	utils::str2int(id, &(rev2.value.number));
	if (rev2.value.number <= 0) {
		svn_pool_destroy(pool);
		return Diffstat();
	}
	rev1.value.number = rev2.value.number - 1;

	apr_file_t *infile = NULL, *outfile = NULL, *errfile = NULL;
	apr_file_open_stderr(&errfile, pool);
	if (apr_file_pipe_create_ex(&infile, &outfile, APR_FULL_BLOCK, pool) != APR_SUCCESS) {
		throw PEX("Unable to create pipe");
	}

	AprStreambuf buf(infile);
	std::istream in(&buf);
	DiffParser parser(in);
	parser.start();

	svn_error_t *err = svn_client_diff4(apr_array_make(pool, 0, 1), d->url, &rev1, d->url, &rev2, NULL, svn_depth_infinity, FALSE, FALSE, TRUE, APR_LOCALE_CHARSET, outfile, errfile, NULL, d->ctx, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}

	apr_file_close(outfile);
	parser.wait();
	apr_file_close(infile);
	svn_pool_destroy(pool);
	return parser.stat();
}

// Returns a log iterator for the given branch
Backend::LogIterator *SubversionBackend::iterator(const std::string &branch)
{
	std::string prefix;
	if (branch == "trunk") {
		prefix = branch;
	} else if (!branch.empty()) {
		prefix = "branches/";
		prefix += branch;
	}

	// Check if the branch exists
	apr_pool_t *pool = svn_pool_create(d->pool);
	svn_dirent_t *dirent;
	svn_error_t *err = svn_ra_stat(d->ra, prefix.c_str(), SVN_INVALID_REVNUM, &dirent, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}
	if (dirent == NULL) {
		if (prefix == "trunk") {
			prefix.clear();
		} else {
			throw PEX(utils::strprintf("No such branch: %s", branch.c_str()));
		}
	}
	svn_pool_destroy(pool);

	long int headrev;
	utils::str2int(head(prefix), &headrev);
	return new SubversionLogIterator(d, prefix, headrev);
}

// Adds the given revision IDs to the diffstat scheduler
void SubversionBackend::prefetch(const std::vector<std::string> &ids)
{
	if (ids.empty()) {
		return;
	}
	if (m_sched == NULL) {
		m_sched = new SvnDiffstatScheduler(d->url, m_opts.authData(), 10);
		m_sched->start();
	}
	m_sched->add(ids);
}

// Handle cleanup of diffstat scheduler
void SubversionBackend::finalize()
{
	if (m_sched) {
		m_sched->finish();
		m_sched->wait();
		delete m_sched;
		m_sched = NULL;
	}
}

// Returns the revision data for the given ID
Revision *SubversionBackend::revision(const std::string &id)
{
	std::map<std::string, std::string> data;

	apr_pool_t *pool = svn_pool_create(d->pool);
	apr_hash_t *props;
	svn_revnum_t revnum;
	utils::str2int(id, &(revnum));

	svn_error_t *err = svn_ra_rev_proplist(d->ra, revnum, &props, pool);
	if (err != NULL) {
		throw PEX(SvnConnection::strerr(err));
	}

	svn_string_t *value;
	std::string author, message;
	int64_t date = 0;
	if ((value = static_cast<svn_string_t *>(apr_hash_get(props, "svn:author", APR_HASH_KEY_STRING)))) {
		author = value->data;
	}
	if ((value = static_cast<svn_string_t *>(apr_hash_get(props, "svn:date", APR_HASH_KEY_STRING)))) {
		apr_time_t when;
		if ((err = svn_time_from_cstring(&when, value->data, pool)) != NULL) {
			throw PEX(SvnConnection::strerr(err));
		}
		date = apr_time_sec(when);
	}
	if ((value = static_cast<svn_string_t *>(apr_hash_get(props, "svn:log", APR_HASH_KEY_STRING)))) {
		message = value->data;
	}

	svn_pool_destroy(pool);
	return new Revision(id, date, author, message, diffstat(id));
}
