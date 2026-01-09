# fstypename.m4
# serial 6
dnl Copyright (C) 1998-1999, 2001, 2004, 2006, 2009-2025 Free Software
dnl Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.
dnl This file is offered as-is, without any warranty.

dnl From Jim Meyering.
dnl
dnl See if struct statfs has the f_fstypename member.
dnl If so, define HAVE_STRUCT_STATFS_F_FSTYPENAME.
dnl

AC_DEFUN([gl_FSTYPENAME],
[
  AC_CHECK_MEMBERS([struct statfs.f_fstypename],,,
    [
      #include <sys/types.h>
      #include <sys/param.h>
      #include <sys/mount.h>
    ])
])
