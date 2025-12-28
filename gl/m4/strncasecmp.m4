# strncasecmp.m4
# serial 2
dnl Copyright (C) 2002-2025 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.
dnl This file is offered as-is, without any warranty.

AC_DEFUN([gl_FUNC_STRNCASECMP],
[
  AC_REQUIRE([gl_STRINGS_H_DEFAULTS])
  AC_CHECK_FUNCS([strncasecmp])
  if test $ac_cv_func_strncasecmp = yes; then
    dnl Assume that strncasecmp and strcasecmp share the same bugs.
    gl_STRCASECMP_WORKS
    case "$gl_cv_func_strcasecmp_works" in
      *yes) ;;
      *) REPLACE_STRNCASECMP=1 ;;
    esac
  else
    HAVE_STRNCASECMP=0
  fi
  AC_CHECK_DECLS([strncasecmp])
  if test $ac_cv_have_decl_strncasecmp = no; then
    HAVE_DECL_STRNCASECMP=0
  fi
])

# Prerequisites of lib/strncasecmp.c.
AC_DEFUN([gl_PREREQ_STRNCASECMP], [
  :
])
