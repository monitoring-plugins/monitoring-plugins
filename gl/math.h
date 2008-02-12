/* DO NOT EDIT! GENERATED AUTOMATICALLY! */
/* A GNU-like <math.h>.

   Copyright (C) 2002-2003, 2007 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef _GL_MATH_H

/* The include_next requires a split double-inclusion guard.  */
#include_next <math.h>

#ifndef _GL_MATH_H
#define _GL_MATH_H


/* The definition of GL_LINK_WARNING is copied here.  */
/* GL_LINK_WARNING("literal string") arranges to emit the literal string as
   a linker warning on most glibc systems.
   We use a linker warning rather than a preprocessor warning, because
   #warning cannot be used inside macros.  */
#ifndef GL_LINK_WARNING
  /* This works on platforms with GNU ld and ELF object format.
     Testing __GLIBC__ is sufficient for asserting that GNU ld is in use.
     Testing __ELF__ guarantees the ELF object format.
     Testing __GNUC__ is necessary for the compound expression syntax.  */
# if defined __GLIBC__ && defined __ELF__ && defined __GNUC__
#  define GL_LINK_WARNING(message) \
     GL_LINK_WARNING1 (__FILE__, __LINE__, message)
#  define GL_LINK_WARNING1(file, line, message) \
     GL_LINK_WARNING2 (file, line, message)  /* macroexpand file and line */
#  define GL_LINK_WARNING2(file, line, message) \
     GL_LINK_WARNING3 (file ":" #line ": warning: " message)
#  define GL_LINK_WARNING3(message) \
     ({ static const char warning[sizeof (message)]		\
          __attribute__ ((__unused__,				\
                          __section__ (".gnu.warning"),		\
                          __aligned__ (1)))			\
          = message "\n";					\
        (void)0;						\
     })
# else
#  define GL_LINK_WARNING(message) ((void) 0)
# endif
#endif


#ifdef __cplusplus
extern "C" {
#endif


/* Write x as
     x = mantissa * 2^exp
   where
     If x finite and nonzero: 0.5 <= |mantissa| < 1.0.
     If x is zero: mantissa = x, exp = 0.
     If x is infinite or NaN: mantissa = x, exp unspecified.
   Store exp in *EXPPTR and return mantissa.  */
#if 0
# if 0
#  define frexp rpl_frexp
extern double frexp (double x, int *expptr);
# endif
#elif defined GNULIB_POSIXCHECK
# undef frexp
# define frexp(x,e) \
    (GL_LINK_WARNING ("frexp is unportable - " \
                      "use gnulib module frexp for portability"), \
     frexp (x, e))
#endif


#if 0 || !1
extern long double acosl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef acosl
# define acosl(x) \
    (GL_LINK_WARNING ("acosl is unportable - " \
                      "use gnulib module mathl for portability"), \
     acosl (x))
#endif


#if 0 || !1
extern long double asinl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef asinl
# define asinl(x) \
    (GL_LINK_WARNING ("asinl is unportable - " \
                      "use gnulib module mathl for portability"), \
     asinl (x))
#endif


#if 0 || !1
extern long double atanl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef atanl
# define atanl(x) \
    (GL_LINK_WARNING ("atanl is unportable - " \
                      "use gnulib module mathl for portability"), \
     atanl (x))
#endif


#if 0
# if 0
#  define ceilf rpl_ceilf
extern float ceilf (float x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef ceilf
# define ceilf(x) \
    (GL_LINK_WARNING ("ceilf is unportable - " \
                      "use gnulib module ceilf for portability"), \
     ceilf (x))
#endif

#if 0
# if 0
#  define ceill rpl_ceill
extern long double ceill (long double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef ceill
# define ceill(x) \
    (GL_LINK_WARNING ("ceill is unportable - " \
                      "use gnulib module ceill for portability"), \
     ceill (x))
#endif


#if 0 || !1
extern long double cosl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef cosl
# define cosl(x) \
    (GL_LINK_WARNING ("cosl is unportable - " \
                      "use gnulib module mathl for portability"), \
     cosl (x))
#endif


#if 0 || !1
extern long double expl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef expl
# define expl(x) \
    (GL_LINK_WARNING ("expl is unportable - " \
                      "use gnulib module mathl for portability"), \
     expl (x))
#endif


#if 1
# if 0
#  define floorf rpl_floorf
extern float floorf (float x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef floorf
# define floorf(x) \
    (GL_LINK_WARNING ("floorf is unportable - " \
                      "use gnulib module floorf for portability"), \
     floorf (x))
#endif

#if 0
# if 0
#  define floorl rpl_floorl
extern long double floorl (long double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef floorl
# define floorl(x) \
    (GL_LINK_WARNING ("floorl is unportable - " \
                      "use gnulib module floorl for portability"), \
     floorl (x))
#endif


/* Write x as
     x = mantissa * 2^exp
   where
     If x finite and nonzero: 0.5 <= |mantissa| < 1.0.
     If x is zero: mantissa = x, exp = 0.
     If x is infinite or NaN: mantissa = x, exp unspecified.
   Store exp in *EXPPTR and return mantissa.  */
#if 0 && 0
# define frexpl rpl_frexpl
#endif
#if (0 && 0) || !1
extern long double frexpl (long double x, int *expptr);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef frexpl
# define frexpl(x,e) \
    (GL_LINK_WARNING ("frexpl is unportable - " \
                      "use gnulib module frexpl for portability"), \
     frexpl (x, e))
#endif


/* Return x * 2^exp.  */
#if 0 && 0
# define ldexpl rpl_ldexpl
#endif
#if (0 && 0) || !1
extern long double ldexpl (long double x, int exp);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef ldexpl
# define ldexpl(x,e) \
    (GL_LINK_WARNING ("ldexpl is unportable - " \
                      "use gnulib module ldexpl for portability"), \
     ldexpl (x, e))
#endif


#if 0 || !1
extern long double logl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef logl
# define logl(x) \
    (GL_LINK_WARNING ("logl is unportable - " \
                      "use gnulib module mathl for portability"), \
     logl (x))
#endif


#if 0
# if 0
#  undef roundf
#  define roundf rpl_roundf
extern float roundf (float x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef roundf
# define roundf(x) \
    (GL_LINK_WARNING ("roundf is unportable - " \
                      "use gnulib module roundf for portability"), \
     roundf (x))
#endif

#if 0
# if 0
#  undef round
#  define round rpl_round
extern double round (double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef round
# define round(x) \
    (GL_LINK_WARNING ("round is unportable - " \
                      "use gnulib module round for portability"), \
     round (x))
#endif

#if 0
# if 0
#  undef roundl
#  define roundl rpl_roundl
extern long double roundl (long double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef roundl
# define roundl(x) \
    (GL_LINK_WARNING ("roundl is unportable - " \
                      "use gnulib module roundl for portability"), \
     roundl (x))
#endif


#if 0 || !1
extern long double sinl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef sinl
# define sinl(x) \
    (GL_LINK_WARNING ("sinl is unportable - " \
                      "use gnulib module mathl for portability"), \
     sinl (x))
#endif


#if 0 || !1
extern long double sqrtl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef sqrtl
# define sqrtl(x) \
    (GL_LINK_WARNING ("sqrtl is unportable - " \
                      "use gnulib module mathl for portability"), \
     sqrtl (x))
#endif


#if 0 || !1
extern long double tanl (long double x);
#endif
#if !0 && defined GNULIB_POSIXCHECK
# undef tanl
# define tanl(x) \
    (GL_LINK_WARNING ("tanl is unportable - " \
                      "use gnulib module mathl for portability"), \
     tanl (x))
#endif


#if 0
# if !1
#  define truncf rpl_truncf
extern float truncf (float x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef truncf
# define truncf(x) \
    (GL_LINK_WARNING ("truncf is unportable - " \
                      "use gnulib module truncf for portability"), \
     truncf (x))
#endif

#if 0
# if !1
#  define trunc rpl_trunc
extern double trunc (double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef trunc
# define trunc(x) \
    (GL_LINK_WARNING ("trunc is unportable - " \
                      "use gnulib module trunc for portability"), \
     trunc (x))
#endif

#if 0
# if !1
#  define truncl rpl_truncl
extern long double truncl (long double x);
# endif
#elif defined GNULIB_POSIXCHECK
# undef truncl
# define truncl(x) \
    (GL_LINK_WARNING ("truncl is unportable - " \
                      "use gnulib module truncl for portability"), \
     truncl (x))
#endif


#if 0
# if 0
extern int gl_isfinitef (float x);
extern int gl_isfinited (double x);
extern int gl_isfinitel (long double x);
#  undef isfinite
#  define isfinite(x) \
   (sizeof (x) == sizeof (long double) ? gl_isfinitel (x) : \
    sizeof (x) == sizeof (double) ? gl_isfinited (x) : \
    gl_isfinitef (x))
# endif
#elif defined GNULIB_POSIXCHECK
  /* How to override a macro?  */
#endif


#if 0
# if 0
#  undef signbit
   /* GCC 4.0 and newer provides three built-ins for signbit.  */
#  define signbit(x) \
   (sizeof (x) == sizeof (long double) ? __builtin_signbitl (x) : \
    sizeof (x) == sizeof (double) ? __builtin_signbit (x) : \
    __builtin_signbitf (x))
# endif
# if 0
#  undef signbit
extern int gl_signbitf (float arg);
extern int gl_signbitd (double arg);
extern int gl_signbitl (long double arg);
#  if __GNUC__ >= 2 && !__STRICT_ANSI__
#   if defined FLT_SIGNBIT_WORD && defined FLT_SIGNBIT_BIT
#    define gl_signbitf(arg) \
       ({ union { float _value;						\
                  unsigned int _word[(sizeof (float) + sizeof (unsigned int) - 1) / sizeof (unsigned int)]; \
                } _m;							\
          _m._value = (arg);						\
          (_m._word[FLT_SIGNBIT_WORD] >> FLT_SIGNBIT_BIT) & 1;		\
        })
#   endif
#   if defined DBL_SIGNBIT_WORD && defined DBL_SIGNBIT_BIT
#    define gl_signbitd(arg) \
       ({ union { double _value;						\
                  unsigned int _word[(sizeof (double) + sizeof (unsigned int) - 1) / sizeof (unsigned int)]; \
                } _m;							\
          _m._value = (arg);						\
          (_m._word[DBL_SIGNBIT_WORD] >> DBL_SIGNBIT_BIT) & 1;		\
        })
#   endif
#   if defined LDBL_SIGNBIT_WORD && defined LDBL_SIGNBIT_BIT
#    define gl_signbitl(arg) \
       ({ union { long double _value;					\
                  unsigned int _word[(sizeof (long double) + sizeof (unsigned int) - 1) / sizeof (unsigned int)]; \
                } _m;							\
          _m._value = (arg);						\
          (_m._word[LDBL_SIGNBIT_WORD] >> LDBL_SIGNBIT_BIT) & 1;		\
        })
#   endif
#  endif
#  define signbit(x) \
   (sizeof (x) == sizeof (long double) ? gl_signbitl (x) : \
    sizeof (x) == sizeof (double) ? gl_signbitd (x) : \
    gl_signbitf (x))
# endif
#elif defined GNULIB_POSIXCHECK
  /* How to override a macro?  */
#endif


#ifdef __cplusplus
}
#endif

#endif /* _GL_MATH_H */
#endif /* _GL_MATH_H */
