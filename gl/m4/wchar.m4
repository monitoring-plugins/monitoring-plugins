dnl A placeholder for ISO C99 <wchar.h>, for platforms that have issues.

dnl Copyright (C) 2007 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Written by Eric Blake.

# wchar.m4 serial 1

AC_DEFUN([gl_WCHAR_H],
[
  AC_CACHE_CHECK([whether <wchar.h> is standalone],
    [gl_cv_header_wchar_h_standalone],
    [AC_COMPILE_IFELSE([[#include <wchar.h>
wchar_t w;]],
      [gl_cv_header_wchar_h_standalone=yes],
      [gl_cv_header_wchar_h_standalone=no])])
  if test $gl_cv_header_wchar_h_standalone = yes; then
    WCHAR_H=
  else
    gl_ABSOLUTE_HEADER([wchar.h])
    ABSOLUTE_WCHAR_H=\"$gl_cv_absolute_wchar_h\"
    WCHAR_H=wchar.h
  fi
  AC_SUBST([ABSOLUTE_WCHAR_H])
  AC_SUBST([WCHAR_H])
])
