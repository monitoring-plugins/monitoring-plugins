# strcasecmp.m4
# serial 3
dnl Copyright (C) 2002-2025 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.
dnl This file is offered as-is, without any warranty.

AC_DEFUN([gl_FUNC_STRCASECMP],
[
  AC_REQUIRE([gl_STRINGS_H_DEFAULTS])
  AC_CHECK_FUNCS([strcasecmp])
  if test $ac_cv_func_strcasecmp = yes; then
    gl_STRCASECMP_WORKS
    case "$gl_cv_func_strcasecmp_works" in
      *yes) ;;
      *) REPLACE_STRCASECMP=1 ;;
    esac
  else
    HAVE_STRCASECMP=0
  fi
])

AC_DEFUN([gl_STRCASECMP_WORKS],
[
  AC_REQUIRE([AC_CANONICAL_HOST])
  AC_CACHE_CHECK([whether strcasecmp works],
    [gl_cv_func_strcasecmp_works],
    [dnl Prepare a guess, used when cross-compiling or when specific locales
     dnl are not available.
     case "$host_os" in
       solaris* | cygwin*)
         gl_cv_func_strcasecmp_works="guessing no" ;;
       *)
         gl_cv_func_strcasecmp_works="guessing yes" ;;
     esac
     AC_RUN_IFELSE(
       [AC_LANG_SOURCE([[
#include <stdio.h>
#include <ctype.h>
#include <locale.h>
#include <strings.h>
int main ()
{
  if (setlocale (LC_ALL, "fr_FR.ISO-8859-1") != NULL
      || setlocale (LC_ALL, "fr_FR.ISO8859-1") != NULL)
    {
      int c1 = (unsigned char) '\311';
      int c2 = (unsigned char) '\351';
      if (tolower (c1) == c2 && toupper (c2) == c1)
        return strcasecmp ("Fej\311r", "Fej\351r") != 0;
    }
  return 2;
}]])],
       [gl_cv_func_strcasecmp_works=yes],
       [if test $? = 1; then
          gl_cv_func_strcasecmp_works=no
        fi
       ],
       [:])
    ])
])

# Prerequisites of lib/strcasecmp.c.
AC_DEFUN([gl_PREREQ_STRCASECMP], [
  :
])
