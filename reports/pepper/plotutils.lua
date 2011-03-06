--- Common utility functions for plotting.
--  Please note that this is a Lua module. If you want to use it, add
--  <pre>require "pepper.plotutils"</pre> to your script. The functions
--  in this module are provided to facilate common plotting tasks and
--  to remove duplicate code for the built-in reports.

module("pepper.plotutils", package.seeall)


--- Converts from UNIX to Gnuplot epoch.
--  @param time UNIX timestamp
function convepoch(time)
	return time - 946684800
end

--- Adds x2tics for repository tags.
--  If <code>pattern</code> is not <code>nil</code>, only tags matching
--  the <code>pattern</code> will be added.
--  @param plot pepper.plot obejct
--  @param repo pepper.repository object
--  @param pattern Optional pattern for filtering tags
function add_tagmarks(plot, repo, pattern)
	-- Fetch tags and generate tic data
	local tags = repo:tags()
	if #tags == 0 then
		return
	end
	local x2tics = "("
	for k,v in ipairs(tags) do
		if pattern == nil or v:name():find(pattern) ~= nil then
			x2tics = x2tics .. "\"" .. v:name() .. "\" " .. convepoch(repo:revision(v:id()):date()) .. ","
		end
	end
	if #x2tics == 1 then
		return
	end
	x2tics = x2tics:sub(0, #x2tics-1) .. ")"
	plot:cmd([[
set x2data time
set format x2 "%s"
set x2tics scale 0
set x2tics border rotate by 60
set x2tics font "Helvetica,8"
set grid x2tics
]])
	plot:cmd("set x2tics " .. x2tics)
end

--- Performs a standard plot setup for time data.
--  Basically, this evaluates to the following GNUPlot commands (without comments):
--  <pre> set xdata time           # X values of data are time values<br>
--  set timefmt "%s"         # Time values are given as UNIX timestamps<br>
--  set yrange [0:*]         # Start Y axis at 0<br>
--  set xtics nomirror       # Don't mirror X axis tics<br>
--  set xtics rotate by -45  # Rotate X axis labels<br>
--  set rmargin 8            # Make sure there's enough space for the rotated labels<br>
--  set grid ytics           # Show grid lines for the Y axis tics<br></pre>
--  The <code>options</code> parameter can be used to customize the
--  plot. The following keys are supported:
--  <table>
--  <tr><th>Key</th><th>Description</th><th>Default value</th></tr>
--  <tr><td><code>key</code></td><td>Key position</td><td>No key</td></tr>
--  <tr><td><code>xformat</code></td><td>X axis labels format</td><td><code>"%b %y"</code></td></tr>
--  <tr><td><code>yformat</code></td><td>Y axis labels format</td><td><code>"%.0f"</code></td></tr>
--  </table><br>
--  @param plot pepper.plot object
--  @param options Optional dictionary with additional options
function setup_std_time(plot, options)
	plot:cmd([[
set xdata time
set timefmt "%s"
set yrange [0:*]
set xtics nomirror
set xtics rotate by -45
set rmargin 8
set grid ytics
]])
	if options == nil then
		options = {}
	end
	if options.key ~= nil then plot:cmd("set key box; set key " .. options.key) end
	if options.xformat == nil then options.xformat = "%b %y" end
	plot:cmd("set format x \"" .. options.xformat .. "\"")
	if options.yformat == nil then options.yformat = "%.0f" end
	plot:cmd("set format y \"" .. options.yformat .. "\"")
end
