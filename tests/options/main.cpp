/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010 Jonas Gehring
 *
 * file: tests/options/main.cpp
 * Short program for testing different command lines
 */


#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "options.h"
#include "logger.h"


typedef std::map<std::string, std::string> stringmap;

struct testdata_t {
	char **args;
	stringmap options;
	stringmap scriptOptions;
	stringmap backendOptions;

	testdata_t() { }
	testdata_t(const Options &defaults) {
		options = defaults.m_options;
		scriptOptions = defaults.m_scriptOptions;
		backendOptions = defaults.m_backendOptions;
	}
};

// Sets up an argument list
static char **setupArgs(int n, ...)
{
	char **args = new char*[n+2];
	args[0] = (char *)"pepper";
	va_list vl;
	va_start(vl, n);
	for (int i = 0; i < n; i++) {
		args[i+1] = va_arg(vl, char *);
	}
	args[n+1] = NULL;
	va_end(vl);
	return args;
}

// Sets up testing data
static std::vector<testdata_t> setupTestData()
{
	std::vector<testdata_t> tests;
	Options defaults;

	testdata_t help1(defaults);
	help1.args = setupArgs(1, "-h");
	help1.options["help"] = "true";
	tests.push_back(help1);

	testdata_t help2(defaults);
	help2.args = setupArgs(1, "--help");
	help2.options["help"] = "true";
	tests.push_back(help2);

	testdata_t help3(defaults);
	help3.args = setupArgs(1, "-?");
	help3.options["help"] = "true";
	tests.push_back(help3);

	testdata_t version(defaults);
	version.args = setupArgs(1, "--version");
	version.options["version"] = "true";
	tests.push_back(version);

	testdata_t simple(defaults);
	simple.args = setupArgs(2, "loc", "http://svn.example.org");
	simple.options["script"] = "loc";
	simple.options["url"] = "http://svn.example.org";
	tests.push_back(simple);

	testdata_t script(defaults);
	script.args = setupArgs(4, "loc", "--branch=trunk", "-tpng", "http://svn.example.org");
	script.options["script"] = "loc";
	script.options["url"] = "http://svn.example.org";
	script.scriptOptions["branch"] = "trunk";
	script.scriptOptions["t"] = "png";
	tests.push_back(script);

	testdata_t backend(defaults);
	backend.args = setupArgs(4, "svn", "authors", "-tpng", "http://svn.example.org");
	backend.options["forced_backend"] = "svn";
	backend.options["script"] = "authors";
	backend.options["url"] = "http://svn.example.org";
	backend.scriptOptions["t"] = "png";
	tests.push_back(backend);

	testdata_t backend2(defaults);
	backend2.args = setupArgs(6, "svn", "--username=test", "--non-interactive", "authors", "-tpng", "http://svn.example.org");
	backend2.options["forced_backend"] = "svn";
	backend2.options["script"] = "authors";
	backend2.options["url"] = "http://svn.example.org";
	backend2.scriptOptions["t"] = "png";
	backend2.backendOptions["username"] = "test";
	backend2.backendOptions["non-interactive"] = std::string();
	tests.push_back(backend2);

	testdata_t full(defaults);
	full.args = setupArgs(8, "-v", "--no-cache", "svn", "--username=test", "--non-interactive", "authors", "-tpng", "http://svn.example.org");
	full.options["forced_backend"] = "svn";
	full.options["cache"] = "false";
	full.options["script"] = "authors";
	full.options["url"] = "http://svn.example.org";
	full.scriptOptions["t"] = "png";
	full.backendOptions["username"] = "test";
	full.backendOptions["non-interactive"] = std::string();
	tests.push_back(full);

	return tests;
}

static void compare(const stringmap &map1, const stringmap &map2)
{
	for (stringmap::const_iterator it = map1.begin(); it != map1.end(); ++it) {
		if (map2.find(it->first) == map2.end()) {
			std::cout << " > " << it->first << std::endl;
		} else if (map2.find(it->first)->second != it->second) {
			std::cout << "!= " << it->first << " (" << it->second << " != " << map2.find(it->first)->second << ")" << std::endl;
		}
	}
	for (stringmap::const_iterator it = map1.begin(); it != map1.end(); ++it) {
		if (map1.find(it->first) == map1.end()) {
			std::cout << " < " << it->second << std::endl;
		}
	}
}

// Program entry point
int main(int, char **)
{
	std::stringstream s;
	Logger::setOutput(s);

	std::vector<testdata_t> data = setupTestData();
	bool ok = true;
	for (unsigned int i = 0; i < data.size(); i++) {
		Options opts;
		int argc = 0;
		while (data[i].args[argc++] != NULL);
		opts.parse(argc-1, data[i].args);

		if (!std::equal(opts.m_options.begin(), opts.m_options.end(), data[i].options.begin())) {
			std::cerr << std::endl << i << ": Error: Options don't match!" << std::endl;
			compare(opts.m_options, data[i].options);
			ok = false;
		} else if (!std::equal(opts.m_backendOptions.begin(), opts.m_backendOptions.end(), data[i].backendOptions.begin())) {
			std::cerr << std::endl << i << ": Error: Backed options don't match!" << std::endl;
			compare(opts.m_backendOptions, data[i].backendOptions);
			ok = false;
		} else if (!std::equal(opts.m_scriptOptions.begin(), opts.m_scriptOptions.end(), data[i].scriptOptions.begin())) {
			std::cerr << std::endl << i << ": Error: Script options don't match!" << std::endl;
			compare(opts.m_scriptOptions, data[i].scriptOptions);
			ok = false;
		} else {
			std::cout << i << " " << std::flush;
		}
	}

	if (ok) {
		std::cout << "OK" << std::endl;
		return EXIT_SUCCESS;
	}
	std::cout << "FAILED" << std::endl;
	return EXIT_FAILURE;
}
