# inet_ntop.m4 serial 12
dnl Copyright (C) 2005, 2006, 2008, 2009, 2010 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_INET_NTOP],
[
  dnl Persuade Solaris <arpa/inet.h> to declare inet_ntop.
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])

  gl_REPLACE_ARPA_INET_H

  dnl Most platforms that provide inet_ntop define it in libc.
  dnl Solaris 8..10 provide inet_ntop in libnsl instead.
  gl_save_LIBS=$LIBS
  AC_SEARCH_LIBS([inet_ntop], [nsl], [],
    [AC_REPLACE_FUNCS([inet_ntop])])
  LIBS=$gl_save_LIBS
  INET_NTOP_LIB=
  if test "$ac_cv_search_inet_ntop" != "no" &&
     test "$ac_cv_search_inet_ntop" != "none required"; then
    INET_NTOP_LIB="$ac_cv_search_inet_ntop"
  fi
  AC_SUBST([INET_NTOP_LIB])

  gl_PREREQ_INET_NTOP
])

# Prerequisites of lib/inet_ntop.c.
AC_DEFUN([gl_PREREQ_INET_NTOP], [
  AC_CHECK_DECLS([inet_ntop],,,[#include <arpa/inet.h>])
  if test $ac_cv_have_decl_inet_ntop = no; then
    HAVE_DECL_INET_NTOP=0
  fi
  AC_REQUIRE([gl_SOCKET_FAMILIES])
  AC_REQUIRE([AC_C_RESTRICT])
])
