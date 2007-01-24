# unistd_h.m4 serial 3
dnl Copyright (C) 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Written by Simon Josefsson

AC_DEFUN([gl_HEADER_UNISTD],
[
  AC_CHECK_HEADERS([unistd.h], [
    UNISTD_H=''
  ], [
    UNISTD_H='unistd.h'
  ])
  AC_SUBST(UNISTD_H)
  dnl This module decides to build unistd.h if it is missing.
  dnl The fchdir module decides to build unistd.h if fchdir() is missing.
  dnl Therefore check for the prerequisites of lib/unistd.h always.
  gl_PREREQ_UNISTD
])

dnl Prerequisites of lib/unistd.h.
AC_DEFUN([gl_PREREQ_UNISTD],
[
  AC_CHECK_HEADERS_ONCE([unistd.h])
  if test $ac_cv_header_unistd_h = yes; then
    gl_ABSOLUTE_HEADER([unistd.h])
    ABSOLUTE_UNISTD_H=\"$gl_cv_absolute_unistd_h\"
  fi
  AC_SUBST([ABSOLUTE_UNISTD_H])
])
