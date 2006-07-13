dnl These are for the libs and macros from coreutils
dnl Currently utilising v5.2.1

dnl We use jm_ for non Autoconf macros.
m4_pattern_forbid([^np_[ABCDEFGHIJKLMNOPQRSTUVXYZ]])dnl
m4_pattern_forbid([^jm_[ABCDEFGHIJKLMNOPQRSTUVXYZ]])dnl
m4_pattern_forbid([^gl_[ABCDEFGHIJKLMNOPQRSTUVXYZ]])dnl

dnl These are all m4 things that need to be called
dnl Usually in coreutils' prereq.m4, but this is a subset that we need
AC_DEFUN([np_COREUTILS],
[
  AC_REQUIRE([AM_STDBOOL_H])
  AC_REQUIRE([gl_BASENAME])
  AC_REQUIRE([gl_C_STRTOLD])
  AC_REQUIRE([gl_EXITFAIL])
  AC_REQUIRE([gl_FCNTL_SAFER])
  AC_REQUIRE([gl_FSUSAGE])
  AC_REQUIRE([gl_FUNC_ALLOCA])
  AC_REQUIRE([gl_GETOPT])
  AC_REQUIRE([gl_MOUNTLIST])
  AC_REQUIRE([gl_REGEX])
  AC_REQUIRE([gl_UNISTD_SAFER])
  AC_REQUIRE([gl_XALLOC])
  AC_REQUIRE([gl_FUNC_GLIBC_UNLOCKED_IO])

dnl This is in jm-macros.m4, which calls prereq.m4, and then calls this afterwards
  AC_REQUIRE([gl_AFS])
])
