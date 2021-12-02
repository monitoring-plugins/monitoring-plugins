# (this check is rougly based on and inspired libcurl.m4)
# URIPARSER_CHECK ([DEFAULT-ACTION], [MINIMUM-VERSION],
#                       [ACTION-IF-YES], [ACTION-IF-NO])
# Checks for uriparser library.  DEFAULT-ACTION is the string yes or no to
# specify whether to default to --with-uriparser or --without-liburiparser.
# If not supplied, DEFAULT-ACTION is yes.  MINIMUM-VERSION is the
# minimum version of uriparser to accept.  Pass the version as a regular
# version number like 0.8.5. If not supplied, any version is
# accepted.  ACTION-IF-YES is a list of shell commands to run if
# uriparser was successfully found and passed the various tests.
# ACTION-IF-NO is a list of shell commands that are run otherwise.
# Note that using --without-uriparser does run ACTION-IF-NO.
#
# This macro #defines HAVE_URIPARSER if a working uriparser setup is
# found, and sets @URIPARSER@ and @URIPARSER_CPPFLAGS@ to the necessary
# values.
#
# Users may override the detected values by doing something like:
# URIPARSER="-luriparser" URIPARSER_CPPFLAGS="-I/usr/myinclude" ./configure
#

AC_DEFUN([URIPARSER_CHECK],
[
  AC_ARG_WITH(uriparser,
     AS_HELP_STRING([--with-uriparser=PREFIX],[look for the uriparser library in PREFIX/lib and headers in PREFIX/include]),
     [_uriparser_with=$withval],[_uriparser_with=ifelse([$1],,[yes],[$1])])

  if test "$_uriparser_with" != "no" ; then

     _uriparser_try_link=yes

     AC_CHECK_PROG(PKGCONFIG,pkg-config,pkg-config,no)

     if test "x$URIPARSER" != "x" || test "x$URIPARSER_CPPFLAGS" != "x"; then
        :
     elif test -d "$_uriparser_with" ; then
        URIPARSER_CPPFLAGS="-I$withval/include"
        _uriparser_ldflags="-L$withval/lib"

     elif test x$PKGCONFIG != xno; then

        AC_CACHE_CHECK([for the version of uriparser],
           [uriparser_cv_uriparser_version],
           [uriparser_cv_uriparser_version=`$PKGCONFIG liburiparser --modversion`])

        AC_PROG_AWK

        _uriparser_version_parse="eval $AWK '{split(\$NF,A,\".\"); X=256*256*A[[1]]+256*A[[2]]+A[[3]]; print X;}'"

        _uriparser_version=`echo $uriparser_cv_uriparser_version | $_uriparser_version_parse`
        _uriparser_wanted=`echo ifelse([$2],,[0],[$2]) | $_uriparser_version_parse`

        if test $_uriparser_wanted -gt 0 ; then
           AC_CACHE_CHECK([for uriparser >= version $2],
              [uriparser_cv_lib_version_ok],
              [
              if test $_uriparser_version -ge $_uriparser_wanted ; then
                 uriparser_cv_lib_version_ok=yes
              else
                 uriparser_cv_lib_version_ok=no
              fi
              ])
        fi

        if test $_uriparser_wanted -eq 0 || test x$uriparser_cv_lib_version_ok = xyes ; then
           if test x"$URIPARSER_CPPFLAGS" = "x" ; then
              URIPARSER_CPPFLAGS=`$PKGCONFIG liburiparser --cflags`
           fi
           if test x"$URIPARSER" = "x" ; then
              URIPARSER=`$PKGCONFIG liburiparser --libs`
           fi
        else
           _uriparser_try_link=no
        fi

        unset _uriparser_wanted
     else
        dnl no pkg-config, ok, do our best and set some defaults
        URIPARSER_CPPFLAGS="-I/usr/include"
        URIPARSER="-luriparser -L/usr/lib -L/usr/lib64 -L/usr/lib/x86_64-linux-gnu -L/usr/lib/i686-linux-gnu"
     fi

     if test $_uriparser_try_link = yes ; then

        # let's see if the user-supplied
        # link line (or failing that, "-luriparser") is enough.
        URIPARSER=${URIPARSER-"$_uriparser_ldflags -luriparser"}

        AC_CACHE_CHECK([whether uriparser is usable],
           [uriparser_cv_lib_uriparser_usable],
           [
           _liburiparser_save_cppflags=$CPPFLAGS
           CPPFLAGS="$URIPARSER_CPPFLAGS $CPPFLAGS"
           _liburiparser_save_libs=$LIBS
           LIBS="$URIPARSER $LIBS"

           AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <uriparser/Uri.h>]],[[
/* Try and use a few common options to force a failure if we are
   missing symbols or cannot link. */
UriParserStateA state;
UriUriA uri;
state.uri = &uri;
char *location = "http://test.dom/dir/file.ext";
int x = uriParseUriA (&state, location);
if (x == URI_SUCCESS) {;}
]])],uriparser_cv_lib_uriparser_usable=yes,uriparser_cv_lib_uriparser_usable=no)

           CPPFLAGS=$_liburiparser_save_cppflags
           LIBS=$_liburiparser_save_libs
           unset _liburiparser_save_cppflags
           unset _liburiparser_save_libs
           ])

        if test $uriparser_cv_lib_uriparser_usable = yes ; then
           AC_DEFINE(HAVE_URIPARSER,1,
             [Define to 1 if you have a functional uriparser library.])
           AC_SUBST(URIPARSER_CPPFLAGS)
           AC_SUBST(URIPARSER)
        else
           unset URIPARSER
           unset URIPARSER_CPPFLAGS
        fi
     fi

     unset _uriparser_try_link
     unset _uriparser_version_parse
     unset _uriparser_version
     unset _uriparser_ldflags
  fi

  if test x$_uriparser_with = xno || test x$uriparser_cv_lib_uriparser_usable != xyes ; then
     # This is the IF-NO path
     ifelse([$4],,:,[$4])
  else
     # This is the IF-YES path
     ifelse([$3],,:,[$3])
  fi

  unset _uriparser_with
])dnl
