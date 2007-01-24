dnl A placeholder for ISO C99 <wctype.h>, for platforms that lack it.

dnl Copyright (C) 2006, 2007 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl Written by Paul Eggert.

AC_DEFUN([gl_WCTYPE_H],
[
  AC_CHECK_FUNCS_ONCE([iswcntrl])
  AC_CHECK_HEADERS_ONCE([wctype.h])
  AC_REQUIRE([AC_C_INLINE])

  AC_REQUIRE([gt_TYPE_WINT_T])
  if test $gt_cv_c_wint_t = yes; then
    HAVE_WINT_T=1
  else
    HAVE_WINT_T=0
  fi
  AC_SUBST([HAVE_WINT_T])

  WCTYPE_H=wctype.h
  if test $ac_cv_header_wctype_h = yes; then
    if test "$ac_cv_func_iswcntrl" = yes; then
      WCTYPE_H=
    fi
    dnl Compute ABSOLUTE_WCTYPE_H even if WCTYPE_H is empty,
    dnl for the benefit of builds from non-distclean directories.
    gl_ABSOLUTE_HEADER([wctype.h])
    ABSOLUTE_WCTYPE_H=\"$gl_cv_absolute_wctype_h\"
    HAVE_WCTYPE_H=1
  else
    ABSOLUTE_WCTYPE_H=\"no/such/file/wctype.h\"
    HAVE_WCTYPE_H=0
  fi
  AC_SUBST([ABSOLUTE_WCTYPE_H])
  AC_SUBST([HAVE_WCTYPE_H])
  AC_SUBST([WCTYPE_H])
])
