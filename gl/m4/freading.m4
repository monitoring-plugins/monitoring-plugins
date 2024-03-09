# freading.m4 serial 2
dnl Copyright (C) 2007, 2009-2023 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([gl_FUNC_FREADING],
[
  AC_CHECK_HEADERS_ONCE([stdio_ext.h])
  AC_CHECK_FUNCS_ONCE([__freading])
])
