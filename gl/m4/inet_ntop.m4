# inet_ntop.m4 serial 9
dnl Copyright (C) 2005, 2006, 2008, 2009 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_INET_NTOP],
[
  dnl Persuade Solaris <arpa/inet.h> to declare inet_ntop.
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])

  gl_REPLACE_ARPA_INET_H

  dnl The AC_SEARCH_LIBS call is a hack to persuade the Solaris 8 linker to
  dnl find inet_ntop.
  dnl
  dnl It is the responsibility of gl_INET_NTOP's caller to arrange for
  dnl -lnsl if it is needed.  Normally -lnsl is not needed on Solaris 8,
  dnl since inet_ntop is needed only by getaddrinfo, and getaddrinfo
  dnl isn't built on Solaris 8.
  gl_save_LIBS=$LIBS
  AC_SEARCH_LIBS([inet_ntop], [nsl], [],
    [AC_REPLACE_FUNCS([inet_ntop])])
  LIBS=$gl_save_LIBS

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
