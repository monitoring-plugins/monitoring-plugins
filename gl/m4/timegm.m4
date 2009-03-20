# timegm.m4 serial 6
dnl Copyright (C) 2003, 2007, 2009 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_TIMEGM],
[
  AC_REQUIRE([gl_HEADER_TIME_H_DEFAULTS])
  AC_REQUIRE([gl_FUNC_MKTIME])
  if test $ac_cv_func_working_mktime = no; then
    # Assume that timegm is buggy if mktime is.
    AC_LIBOBJ([timegm])
    ac_cv_func_timegm=no
  else
    AC_REPLACE_FUNCS([timegm])
  fi
  REPLACE_TIMEGM=1
  if test $ac_cv_func_timegm = yes; then
    AC_CHECK_DECLS([timegm], [REPLACE_TIMEGM=0], [], [#include <time.h>])
  fi
  if test $REPLACE_TIMEGM = 1; then
    gl_PREREQ_TIMEGM
  fi
])

# Prerequisites of lib/timegm.c.
AC_DEFUN([gl_PREREQ_TIMEGM], [
  AC_REQUIRE([gl_TIME_R])
  AC_REQUIRE([gl_FUNC_MKTIME])
  if test $ac_cv_func_working_mktime = yes; then
    AC_CHECK_FUNC([__mktime_internal], ,
      [# mktime works but it doesn't export __mktime_internal,
       # so we need to substitute our own mktime implementation.
       AC_LIBOBJ([mktime])
       AC_DEFINE([mktime], [rpl_mktime],
	 [Define to rpl_mktime if the replacement function should be used.])
       gl_PREREQ_MKTIME])
  fi
])
