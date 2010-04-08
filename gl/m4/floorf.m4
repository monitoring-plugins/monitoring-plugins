# floorf.m4 serial 6
dnl Copyright (C) 2007, 2009-2010 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_FLOORF],
[
  AC_REQUIRE([gl_MATH_H_DEFAULTS])
  dnl Persuade glibc <math.h> to declare floorf().
  AC_REQUIRE([gl_USE_SYSTEM_EXTENSIONS])
  dnl Test whether floorf() is declared.
  AC_CHECK_DECLS([floorf], , , [#include <math.h>])
  if test "$ac_cv_have_decl_floorf" = yes; then
    dnl Test whether floorf() can be used without libm.
    gl_FUNC_FLOORF_LIBS
    if test "$FLOORF_LIBM" = "?"; then
      dnl Sun C 5.0 on Solaris declares floorf() and has it in the system-wide
      dnl libm.so, but not in the libm.so that the compiler uses.
      REPLACE_FLOORF=1
    fi
  else
    HAVE_DECL_FLOORF=0
  fi
  if test $HAVE_DECL_FLOORF = 0 || test $REPLACE_FLOORF = 1; then
    AC_LIBOBJ([floorf])
    FLOORF_LIBM=
  fi
  AC_SUBST([FLOORF_LIBM])
])

# Determines the libraries needed to get the floorf() function.
# Sets FLOORF_LIBM.
AC_DEFUN([gl_FUNC_FLOORF_LIBS],
[
  gl_CACHE_VAL_SILENT([gl_cv_func_floorf_libm], [
    gl_cv_func_floorf_libm=?
    AC_TRY_LINK([
       #ifndef __NO_MATH_INLINES
       # define __NO_MATH_INLINES 1 /* for glibc */
       #endif
       #include <math.h>
       float x;],
      [x = floorf(x);],
      [gl_cv_func_floorf_libm=])
    if test "$gl_cv_func_floorf_libm" = "?"; then
      save_LIBS="$LIBS"
      LIBS="$LIBS -lm"
      AC_TRY_LINK([
         #ifndef __NO_MATH_INLINES
         # define __NO_MATH_INLINES 1 /* for glibc */
         #endif
         #include <math.h>
         float x;],
        [x = floorf(x);],
        [gl_cv_func_floorf_libm="-lm"])
      LIBS="$save_LIBS"
    fi
  ])
  FLOORF_LIBM="$gl_cv_func_floorf_libm"
])
