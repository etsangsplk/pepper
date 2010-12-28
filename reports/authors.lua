--[[
	Plots a graph visualizing the code contribution by author
--]]


-- Script meta-data
meta.title = "Code contribution by authors"
meta.description = "Contributed lines of code by authors"
meta.options = {{"-bARG, --branch=ARG", "Select branch"},
                {"-tARG, --type=ARG", "Select image type"}}

-- Revision callback function
function callback(r)
	if r:author() == "" then
		return
	end

	-- Accumulate contributed lines
	local s = r:diffstat()
	local loc = 0
	for i,v in ipairs(s:files()) do
		loc = loc + s:lines_added(v)
	end

	-- Save commit and LOC count
	table.insert(commits, {r:date(), r:author(), loc})
	if authors[r:author()] == nil then
		authors[r:author()] = loc
	else
		authors[r:author()] = authors[r:author()] + loc
	end
end

-- Checks whether author a has more LOC than b
function authorcmp(a, b)
	return (a[2] > b[2])
end

-- Checks whether commit a has been earlier than b
function commitcmp(a, b)
	return (a[1] < b[1])
end

-- Main script function
function main()
	commits = {}   -- Commit list by timestamp with LOC delta
	authors = {}   -- Total LOC by author

	-- Gather data
	branch = pepper.report.getopt("b,branch", pepper.report.repository():main_branch())
	pepper.report.walk_branch(callback, branch)

	-- Determine the 6 "busiest" authors (by LOC)
	local authorloc = {}
	for k,v in pairs(authors) do
		table.insert(authorloc, {k, v})
	end
	table.sort(authorloc, authorcmp)
	local i = 7
	while i <= #authorloc do
		authors[authorloc[i][1]] = nil
		authorloc[i] = nil
		i = i + 1
	end

	-- Sort commits by time
	table.sort(commits, commitcmp)

	-- Generate data arrays for the authors
	local keys = {}
	local series = {}
	local loc = {}
	for i,a in ipairs(authorloc) do
		loc[a[1]] = 0
	end
	for t,v in ipairs(commits) do
		table.insert(keys, v[1])
		table.insert(series, {});
		for i,a in ipairs(authorloc) do
			if a[1] == v[2] then
				loc[a[1]] = loc[a[1]] + v[3]
			end
			table.insert(series[#series], loc[a[1]])
		end
	end

	local authors = {}
	for i,a in ipairs(authorloc) do
		table.insert(authors, a[1])
	end

	local p = pepper.gnuplot:new()
	p:set_title("Contributed Lines of Code by Author (on " .. branch .. ")")
	p:set_output("authors." .. pepper.report.getopt("t, type", "svg"), 600, 480)
	p:cmd([[
set xdata time
set timefmt "%s"
set format x "%b %y"
set decimal locale
set format y "%'.0f"
set yrange [0:*]
set xtics nomirror
set xtics rotate by -45
set grid ytics
set rmargin 8
set key box
set key below]])
	p:plot_series(keys, series, authors)
end
