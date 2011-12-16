# np_mysqlclient.m4
dnl Copyright (C) 2007 Nagios Plugins Team
dnl This file is free software; the Nagios Plugin Team
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Test for mysql availability using mysql_config
dnl Uses --with-mysql= yes(autodetection - default) | no | path
dnl Sets 4 variables:
dnl   with_mysql = path/to/mysql_config (if found and can compile mysqlclient) or "no"
dnl   np_mysql_include = flags for include, from mysql_config --include (will be guessed as $with_mysql/include if --include not found)
dnl   np_mysql_libs    = flags for libs,    from mysql_config --libs
dnl   np_mysql_cflags  = flags for cflags,  from mysql_config --cflags
dnl Also sets in config.h:
dnl   HAVE_MYSQLCLIENT
dnl Copile your code with:
dnl   $(CC) $(np_mysql_include) code.c $(np_mysql_libs)

AC_DEFUN([np_mysqlclient],
[
  AC_ARG_WITH(mysql,
    AS_HELP_STRING([--with-mysql=DIR],
      [Locates mysql libraries. Expects DIR/bin/mysql_config. Default to search for mysql_config in PATH]),
    with_mysql=$withval,
    with_mysql=yes)

  if test "x$with_mysql" != "xno" ; then
    if test "x$with_mysql" = "xyes" ; then
      AC_PATH_PROG(np_mysql_config, mysql_config)
    else
      if test -x $with_mysql/bin/mysql_config ; then
        np_mysql_config="$with_mysql/bin/mysql_config"
      fi
    fi
    if test -z "$np_mysql_config"; then
      with_mysql="no"
    else
      np_mysql_include="`$np_mysql_config --include`"
      # Mysql 3 does not support --include. --cflags should be sufficient
      if test $? -ne 0; then
        np_mysql_include="-I$with_mysql/include"	# Guessed location
      fi
      np_mysql_libs="`$np_mysql_config --libs`"
      np_mysql_cflags="`$np_mysql_config --cflags`"
      # On Solaris, cflags may contain -xstrconst, which is not acceptable to the
      # gcc compiler. In this case, use the include flags as the cflags
      echo $np_mysql_cflags | grep -- -xstrconst > /dev/null 2> /dev/null
      if test $? -eq 0 -a "$CC" = "gcc" ; then
        np_mysql_cflags="`$np_mysql_config --include`"
      fi

      dnl Test a mysql_init. Some systems have mysql_config, but no headers
      _savedcppflags="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $np_mysql_include"

      dnl Putting $np_mysql_libs as other libraries ensures that all mysql dependencies are linked in
      dnl Although -lmysqlclient is duplicated, it is not a problem
      AC_CHECK_LIB([mysqlclient], [mysql_init], [
        with_mysql=$np_mysql_config
        AC_DEFINE(HAVE_MYSQLCLIENT, 1, [Defined if mysqlclient is found and can compile]) 
	], [with_mysql=no], [$np_mysql_libs])
      CPPFLAGS=$_savedcppflags

    fi
  fi
])

dnl Will take $1, find last occurrance of -LDIR and add DIR to LD_RUN_PATH
AC_DEFUN([np_add_to_runpath], 
[
  dnl Need [[ ]] so autoconf gives us just one set
  np_libdir=`echo "$1" | sed -e 's/.*-L\([[^ ]]*\) .*/\1/'`
  if test "x$np_libdir" != x ; then
    LD_RUN_PATH="${np_libdir}${LD_RUN_PATH:+:}${LD_RUN_PATH}"
  fi
])

