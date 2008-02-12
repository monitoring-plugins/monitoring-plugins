# vsnprintf.m4 serial 4
dnl Copyright (C) 2002, 2003, 2004, 2007 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_VSNPRINTF],
[
  AC_REQUIRE([gl_STDIO_H_DEFAULTS])
  AC_CHECK_FUNCS([vsnprintf])
  if test $ac_cv_func_vsnprintf = no; then
    gl_REPLACE_VSNPRINTF
  fi
  AC_CHECK_DECLS_ONCE([vsnprintf])
  if test $ac_cv_have_decl_vsnprintf = no; then
    HAVE_DECL_VSNPRINTF=0
  fi
])

AC_DEFUN([gl_REPLACE_VSNPRINTF],
[
  AC_REQUIRE([gl_STDIO_H_DEFAULTS])
  AC_LIBOBJ([vsnprintf])
  if test $ac_cv_func_vsnprintf = yes; then
    REPLACE_VSNPRINTF=1
  fi
  gl_PREREQ_VSNPRINTF
])

# Prerequisites of lib/vsnprintf.c.
AC_DEFUN([gl_PREREQ_VSNPRINTF], [:])
