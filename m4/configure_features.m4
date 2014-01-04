dnl
dnl pepper - SCM statistics report generator
dnl Copyright (C) 2010-2014 Jonas Gehring
dnl
dnl Released under the GNU General Public License, version 3.
dnl Please see the COPYING file in the source distribution for license
dnl terms and conditions, or see http://www.gnu.org/licenses/.
dnl

AC_ARG_ENABLE([gnuplot], [AS_HELP_STRING([--disable-gnuplot], [Disable Gnuplot backend for graphical reports])], [gnuplot="$enableval"], [gnuplot="auto"])
AC_ARG_ENABLE([man], [AS_HELP_STRING([--disable-man], [Don't generate the man page])], [manpage="$enableval"], [manpage="auto"])
AC_ARG_ENABLE([leveldb], [AS_HELP_STRING([--enable-leveldb], [Use LevelDB for caching revisions])], [leveldb="$enableval"], [leveldb="no"])


dnl Run checks for manpage programs
AC_DEFUN([CHECK_MANPROGS], [
	dnl Check for Asciidoc
	AC_ARG_VAR([ASCIIDOC], AsciiDoc executable)
	AC_PATH_PROG([ASCIIDOC], [asciidoc], [not found])
	if test "x$ASCIIDOC" = "xnot found"; then
		if test "x$manpage" = "xyes"; then
			AC_MSG_ERROR([Asciidoc could not be located in your \$PATH])
		else
			manpage="no"
		fi
	else
		ver_info=`$ASCIIDOC --version`
		ver_maj=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\1/'`
		ver_min=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\2/'`
		ver_rev=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\3/'`
		prog_version_ok="yes"
		if test $ver_maj -lt 8; then
			prog_version_ok="no"
		fi
		if test $ver_min -lt 4; then
			prog_version_ok="no"
		fi
		if test $ver_rev -lt 0; then
			prog_version_ok="no"
		fi
		if test "$prog_version_ok" !=  "yes"; then
			if test "x$manpage" = "xyes"; then
				AC_MSG_ERROR([Asciidoc >= 8.4 is needed. Please upgrade your installation])
			else
				manpage="no"
			fi
		fi
	fi

	dnl Check for xmlto
	AC_ARG_VAR([XMLTO], xmlto executable)
	AC_PATH_PROG([XMLTO], [xmlto], [not found])
	if test "x$XMLTO" = "xnot found"; then
		if test "x$manpage" = "xyes"; then
			AC_MSG_ERROR([xmlto could not be located in your \$PATH])
		else
			manpage="no"
		fi
	else
		ver_info=`$XMLTO --version`
		ver_maj=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\1/'`
		ver_min=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\2/'`
		ver_rev=`echo $ver_info | sed 's/^.* \([[0-9]]\)*\.\([[0-9]]\)*\.\([[0-9]]*\).*$/\3/'`
		prog_version_ok="yes"
		if test $ver_maj -lt 0; then
			prog_version_ok="no"
		fi
		if test $ver_min -lt 0; then
			prog_version_ok="no"
		fi
		if test $ver_rev -lt 18; then
			prog_version_ok="no"
		fi
		if test "$prog_version_ok" !=  "yes"; then
			if test "x$manpage" = "xyes"; then
				AC_MSG_ERROR([xmlto >= 0.0.18 is needed. Please upgrade your installation])
			else
				manpage="no"
			fi
		fi
	fi
])

dnl Run checks for LevelDB headers and libraries
AC_DEFUN([CHECK_LEVELDB], [
	AC_ARG_WITH([leveldb], [AC_HELP_STRING([--with-leveldb=PATH], [prefix for LevelDB installation])], [leveldb_prefix=$withval])

	AC_LANG_PUSH([C++])

	builtin_leveldb="no"
	if test "x$leveldb_prefix" = x; then
		dnl No prefix specified. Check system locations first, and fall back
		dnl to builtin version if necessary
		AC_CHECK_HEADER([leveldb/db.h], [header_found="yes"], [header_found="no"])

		if test "x$header_found" = "xyes"; then
			AC_CHECK_LIB([leveldb], [leveldb_open], [lib_found="yes"], [lib_found="no"], [-lpthread])
			if test "x$lib_found" = "xyes"; then
				LEVELDB_LIBS="-lleveldb"
				AC_SUBST([LEVELDB_LIBS])
			fi
		else
			dnl Fall back to builtin in version, and make sure to clear
			dnl the cached header check.
			dnl TODO: A crappy way to get abs_srcdir...
			leveldb_prefix="`cd $srcdir && pwd`/3rdparty/leveldb"
			builtin_leveldb="yes"
			AS_UNSET(AS_TR_SH([ac_cv_header_leveldb/db.h]))
		fi
	fi

	if test "x$leveldb_prefix" != x; then
		dnl Run tests with prefix
		LDB_OLD_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS -I$leveldb_prefix/include"
		AC_CHECK_HEADER([leveldb/db.h], [header_found="yes"], [header_found="no"])
		CPPFLAGS="$LDB_OLD_CPPFLAGS"
		LEVELDB_CPPFLAGS="-I$leveldb_prefix/include"
		AC_SUBST([LEVELDB_CPPFLAGS])

		if test "x$builtin_leveldb" != "xyes"; then
			LDB_OLD_LIBS="$LIBS"
			LIBS="$LIBS -L$leveldb_prefix/lib"
			AC_CHECK_LIB([leveldb], [leveldb_open], [lib_found="yes"], [lib_found="no"], [-lpthread])
			LIBS="$LDB_OLD_LIBS"
			LEVELDB_LIBS="-L$leveldb_prefix/lib -lleveldb"
			AC_SUBST([LEVELDB_LIBS])
		else
			dnl Libs are still to be built
			lib_found="yes"
			LEVELDB_LIBS="-L$leveldb_prefix -lleveldb"
			AC_CHECK_HEADER([snappy.h], [LEVELDB_LIBS="$LEVELDB_LIBS -lsnappy"], [])
			AC_SUBST([LEVELDB_LIBS])
		fi
	fi

    if test "x$header_found" != "xyes" || test "x$lib_found" != "xyes"; then
		if test "x$leveldb" = "xyes"; then
			if test "x$builtin_leveldb" = "xyes"; then
				AC_MSG_ERROR([LevelDB headers or libraries not found.
Make sure you ran 'git submodule init' and 'git submodule update' if
you want to use the builtin version.])
			else
				AC_MSG_ERROR([LevelDB headers or libraries not found.])
			fi
		else
			leveldb="no"
		fi
	fi

	AC_LANG_POP([C++])
])

dnl Run checks for the features
AC_DEFUN([FEATURES_CHECK], [
	if test "x$gnuplot" != "xno"; then
		AC_PATH_PROG([GNUPLOT], [gnuplot], [not found])
		if test "x$GNUPLOT" = "xnot found"; then
			if test "x$gnuplot" = "xyes"; then
				AC_MSG_ERROR([gnuplot could not be located in your \$PATH])
			else
				gnuplot="no"
			fi
		else
			gnuplot="yes"
		fi
	fi

	if test "x$manpage" != "xno"; then
		CHECK_MANPROGS()
		if test "x$manpage" != "xno"; then
			manpage="yes"
		fi
	fi

	if test "x$leveldb" != "xno"; then
		CHECK_LEVELDB()
		if test "x$leveldb" != "xno"; then
			leveldb="yes"
		fi
	fi
])

dnl Print a feature configuration report
AC_DEFUN([FEATURES_REPORT], [
	echo
	echo "    Enabled(+) / disabled(-) features:"
	if test "x$gnuplot" = "xyes"; then echo "      + Gnuplot"; fi
	if test "x$gnuplot" = "xno"; then echo "      - Gnuplot"; fi
	if test "x$manpage" = "xyes"; then echo "      + Manpage"; fi
	if test "x$manpage" = "xno"; then echo "      - Manpage"; fi
	if test "x$leveldb" = "xyes"; then
		if test "x$builtin_leveldb" = "xyes"; then echo "      + LevelDB (builtin)";
		else echo "      + LevelDB"; fi
	fi
	if test "x$leveldb" = "xno"; then echo "      - LevelDB"; fi
])
