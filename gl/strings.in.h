/* A substitute <strings.h>.

   Copyright (C) 2007-2025 Free Software Foundation, Inc.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   This file is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifndef _@GUARD_PREFIX@_STRINGS_H

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

/* Minix 3.1.8 has a bug: <sys/types.h> must be included before <strings.h>.
   But avoid namespace pollution on glibc systems.  */
#if defined __minix && !defined __GLIBC__
# include <sys/types.h>
#endif

/* The include_next requires a split double-inclusion guard.  */
#if @HAVE_STRINGS_H@
# @INCLUDE_NEXT@ @NEXT_STRINGS_H@
#endif

#ifndef _@GUARD_PREFIX@_STRINGS_H
#define _@GUARD_PREFIX@_STRINGS_H

/* This file uses _GL_ARG_NONNULL, GNULIB_POSIXCHECK, HAVE_RAW_DECL_*.  */
#if !_GL_CONFIG_H_INCLUDED
 #error "Please include config.h first."
#endif

#if ! @HAVE_DECL_STRNCASECMP@
/* Get size_t.  */
# include <stddef.h>
#endif

#if @GNULIB_STRCASECMP_L@ || @GNULIB_STRNCASECMP_L@
/* Get locale_t.  */
# include <locale.h>
# if ((__GLIBC__ == 2 && __GLIBC_MINOR__ < 10) \
      || (defined __APPLE__ && defined __MACH__))
/* Get the declaration of strcasecmp_l.  */
#  include <string.h>
# endif
#endif


/* The definitions of _GL_FUNCDECL_RPL etc. are copied here.  */

/* The definition of _GL_ARG_NONNULL is copied here.  */

/* The definition of _GL_WARN_ON_USE is copied here.  */

#ifdef __cplusplus
extern "C" {
#endif


#if @GNULIB_FFS@
/* Find the index of the least-significant set bit.  */
# if !@HAVE_FFS@
_GL_FUNCDECL_SYS (ffs, int, (int i), );
# endif
_GL_CXXALIAS_SYS (ffs, int, (int i));
_GL_CXXALIASWARN (ffs);
#elif defined GNULIB_POSIXCHECK
# undef ffs
# if HAVE_RAW_DECL_FFS
_GL_WARN_ON_USE (ffs, "ffs is not portable - use the ffs module");
# endif
#endif

#if @GNULIB_STRCASECMP@
/* Compare strings S1 and S2, ignoring case, returning less than, equal to or
   greater than zero if S1 is lexicographically less than, equal to or greater
   than S2.
   Note: This function does not work in multibyte locales.  */
# if @REPLACE_STRCASECMP@
#  if !(defined __cplusplus && defined GNULIB_NAMESPACE)
#   undef strcasecmp
#   define strcasecmp rpl_strcasecmp
#  endif
_GL_FUNCDECL_RPL (strcasecmp, int, (const char *, const char *),
                                   _GL_ARG_NONNULL ((1, 2)));
_GL_CXXALIAS_RPL (strcasecmp, int, (const char *, const char *));
# else
#  if !@HAVE_STRCASECMP@
_GL_FUNCDECL_SYS (strcasecmp, int, (const char *, const char *),
                                   _GL_ARG_NONNULL ((1, 2)));
#  endif
_GL_CXXALIAS_SYS (strcasecmp, int, (const char *, const char *));
# endif
# if __GLIBC__ >= 2
_GL_CXXALIASWARN (strcasecmp);
# endif
#elif defined GNULIB_POSIXCHECK
/* strcasecmp() does not work with multibyte strings:
   POSIX says that it operates on "strings", and "string" in POSIX is defined
   as a sequence of bytes, not of characters.   */
# undef strcasecmp
# if HAVE_RAW_DECL_STRCASECMP
_GL_WARN_ON_USE (strcasecmp, "strcasecmp cannot work correctly on character "
                 "strings in multibyte locales and is unportable - "
                 "use mbscasecmp if you care about "
                 "internationalization, or use c_strcasecmp "
                 "(gnulib module c-strcasecmp) if you want a locale "
                 "independent function");
# endif
#endif

#if @GNULIB_STRCASECMP_L@
# if @REPLACE_STRCASECMP_L@
#  if !(defined __cplusplus && defined GNULIB_NAMESPACE)
#   undef strcasecmp_l
#   define strcasecmp_l rpl_strcasecmp_l
#  endif
_GL_FUNCDECL_RPL (strcasecmp_l, int,
                  (const char *s1, const char *s2, locale_t locale),
                  _GL_ARG_NONNULL ((1, 2, 3)));
_GL_CXXALIAS_RPL (strcasecmp_l, int,
                  (const char *s1, const char *s2, locale_t locale));
# else
#  if !@HAVE_STRCASECMP_L@
_GL_FUNCDECL_SYS (strcasecmp_l, int,
                  (const char *s1, const char *s2, locale_t locale),
                  _GL_ARG_NONNULL ((1, 2, 3)));
#  endif
_GL_CXXALIAS_SYS (strcasecmp_l, int,
                  (const char *s1, const char *s2, locale_t locale));
# endif
# if __GLIBC__ >= 2
_GL_CXXALIASWARN (strcasecmp_l);
# endif
#elif defined GNULIB_POSIXCHECK
/* strcasecmp_l() does not work with multibyte strings:
   POSIX says that it operates on "strings", and "string" in POSIX is defined
   as a sequence of bytes, not of characters.   */
# undef strcasecmp_l
# if HAVE_RAW_DECL_STRCASECMP_L
_GL_WARN_ON_USE (strcasecmp_l, "strcasecmp_l cannot work correctly on "
                 "character strings in multibyte locales and is unportable - "
                 "use gnulib module strcasecmp_l for portability");
# endif
#endif

#if @GNULIB_STRNCASECMP@
/* Compare no more than N bytes of strings S1 and S2, ignoring case,
   returning less than, equal to or greater than zero if S1 is
   lexicographically less than, equal to or greater than S2.
   Note: This function cannot work correctly in multibyte locales.  */
# if @REPLACE_STRNCASECMP@
#  if !(defined __cplusplus && defined GNULIB_NAMESPACE)
#   undef strncasecmp
#   define strncasecmp rpl_strncasecmp
#  endif
_GL_FUNCDECL_RPL (strncasecmp, int, (const char *, const char *, size_t),
                                    _GL_ARG_NONNULL ((1, 2)));
_GL_CXXALIAS_RPL (strncasecmp, int, (const char *, const char *, size_t));
# else
#  if !@HAVE_DECL_STRNCASECMP@
_GL_FUNCDECL_SYS (strncasecmp, int, (const char *, const char *, size_t),
                                    _GL_ARG_NONNULL ((1, 2)));
#  endif
_GL_CXXALIAS_SYS (strncasecmp, int, (const char *, const char *, size_t));
# endif
# if __GLIBC__ >= 2
_GL_CXXALIASWARN (strncasecmp);
# endif
#elif defined GNULIB_POSIXCHECK
/* strncasecmp() does not work with multibyte strings:
   POSIX says that it operates on "strings", and "string" in POSIX is defined
   as a sequence of bytes, not of characters.  */
# undef strncasecmp
# if HAVE_RAW_DECL_STRNCASECMP
_GL_WARN_ON_USE (strncasecmp, "strncasecmp cannot work correctly on character "
                 "strings in multibyte locales and is unportable - "
                 "use mbsncasecmp or mbspcasecmp if you care about "
                 "internationalization, or use c_strncasecmp "
                 "(gnulib module c-strncasecmp) if you want a locale "
                 "independent function");
# endif
#endif

#if @GNULIB_STRNCASECMP_L@
# if @REPLACE_STRNCASECMP_L@
#  if !(defined __cplusplus && defined GNULIB_NAMESPACE)
#   undef strncasecmp_l
#   define strncasecmp_l rpl_strncasecmp_l
#  endif
_GL_FUNCDECL_RPL (strncasecmp_l, int,
                  (const char *s1, const char *s2, size_t n, locale_t locale),
                  _GL_ARG_NONNULL ((1, 2, 4)));
_GL_CXXALIAS_RPL (strncasecmp_l, int,
                  (const char *s1, const char *s2, size_t n, locale_t locale));
# else
#  if !@HAVE_STRNCASECMP_L@
_GL_FUNCDECL_SYS (strncasecmp_l, int,
                  (const char *s1, const char *s2, size_t n, locale_t locale),
                  _GL_ARG_NONNULL ((1, 2, 4)));
#  endif
_GL_CXXALIAS_SYS (strncasecmp_l, int,
                  (const char *s1, const char *s2, size_t n, locale_t locale));
# endif
# if __GLIBC__ >= 2
_GL_CXXALIASWARN (strncasecmp_l);
# endif
#elif defined GNULIB_POSIXCHECK
/* strncasecmp_l() does not work with multibyte strings:
   POSIX says that it operates on "strings", and "string" in POSIX is defined
   as a sequence of bytes, not of characters.   */
# undef strncasecmp_l
# if HAVE_RAW_DECL_STRNCASECMP_L
_GL_WARN_ON_USE (strncasecmp_l, "strncasecmp_l cannot work correctly on "
                 "character strings in multibyte locales and is unportable - "
                 "use gnulib module strncasecmp_l for portability");
# endif
#endif


#ifdef __cplusplus
}
#endif

#endif /* _@GUARD_PREFIX@_STRING_H */
#endif /* _@GUARD_PREFIX@_STRING_H */
