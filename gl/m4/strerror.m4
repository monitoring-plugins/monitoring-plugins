# strerror.m4 serial 7
dnl Copyright (C) 2002, 2007 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_STRERROR],
[
  AC_REQUIRE([gl_FUNC_STRERROR_SEPARATE])
  if test $gl_cv_func_working_strerror = no; then
    AC_LIBOBJ([strerror])
    AC_DEFINE_UNQUOTED([REPLACE_STRERROR], [$REPLACE_STRERROR],
      [Define this to 1 if strerror is broken.])
  fi
])

# Like gl_FUNC_STRERROR, except prepare for separate compilation (no AC_LIBOBJ).
AC_DEFUN([gl_FUNC_STRERROR_SEPARATE],
[
  AC_REQUIRE([gl_HEADER_STRING_H_DEFAULTS])
  AC_CACHE_CHECK([for working strerror function],
   [gl_cv_func_working_strerror],
   [AC_RUN_IFELSE(
      [AC_LANG_PROGRAM(
	 [#include <string.h>
	 ],
	 [return !*strerror (-2);])],
      [gl_cv_func_working_strerror=yes],
      [gl_cv_func_working_strerror=no],
      [dnl Assume crossbuild works if it compiles.
       AC_COMPILE_IFELSE(
	 [AC_LANG_PROGRAM(
	    [#include <string.h>
	    ],
	    [return !*strerror (-2);])],
	 [gl_cv_func_working_strerror=yes],
	 [gl_cv_func_working_strerror=no])])])
  if test $gl_cv_func_working_strerror = no ; then
    REPLACE_STRERROR=1
    gl_PREREQ_STRERROR
  fi
])

# Prerequisites of lib/strerror.c.
AC_DEFUN([gl_PREREQ_STRERROR], [
  AC_CHECK_DECLS([strerror])
])
