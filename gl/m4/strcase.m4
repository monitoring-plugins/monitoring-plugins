# strcase.m4 serial 4
dnl Copyright (C) 2002, 2005-2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_STRCASE],
[
  gl_FUNC_STRCASECMP
  gl_FUNC_STRNCASECMP
])

AC_DEFUN([gl_FUNC_STRCASECMP],
[
  dnl No known system has a strcasecmp() function that works correctly in
  dnl multibyte locales. Therefore we use our version always.
  AC_LIBOBJ(strcasecmp)
  gl_PREREQ_STRCASECMP
])

AC_DEFUN([gl_FUNC_STRNCASECMP],
[
  AC_REPLACE_FUNCS(strncasecmp)
  AC_CHECK_DECLS(strncasecmp)
  if test $ac_cv_func_strncasecmp = no; then
    gl_PREREQ_STRNCASECMP
  fi
])

# Prerequisites of lib/strcasecmp.c.
AC_DEFUN([gl_PREREQ_STRCASECMP], [
  AC_REQUIRE([gl_FUNC_MBRTOWC])
  :
])

# Prerequisites of lib/strncasecmp.c.
AC_DEFUN([gl_PREREQ_STRNCASECMP], [
  :
])
