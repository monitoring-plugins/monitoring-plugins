/******************************************************************************
*
* Nagios gethostbyname_r()'s prototype.
*
* License: GPL
* Copyright (C) 2001,2002  Brian Stafford  <brian@stafford.uklinux.net>
*
* Last Modified: $Date$
*
* Description:
*
*  This file is a ghastly hack because nobody can agree on
*  gethostbyname_r()'s prototype.
*
* License Information:
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* $Id$
*****************************************************************************/

/*************************************************************************
   Usage:

   #include <errno.h>
   #include "gethostbyname.h"

   f ()
   {
     struct ghbnctx ctx;

     errno = 0;
     hp = gethostbyname_ctx (host, &ctx);
     if (hp == NULL)
       {
         if (errno != 0)
	   handle_value_of_errno (errno);
	 else
	   handle_value_of_h_errno (h_error_ctx (&ctx));
       }
     else
       {
	 ...
       }
     free_ghbnctx (&ctx);
   }
 *************************************************************************/

#ifndef _gethostbyname_h
#define _gethostbyname_h

#if HAVE_GETIPNODEBYNAME

struct ghbnctx
  {
    int h_err;
    struct hostent *hostent;
  };

#elif HAVE_GETHOSTBYNAME_R == 6

struct ghbnctx
  {
    int h_err;
    struct hostent hostent;
    char *hostbuf;
    size_t hostbuf_len;
  };

#elif HAVE_GETHOSTBYNAME_R == 5

struct ghbnctx
  {
    int h_err;
    struct hostent hostent;
    char *hostbuf;
    int hostbuf_len;
  };

#elif HAVE_GETHOSTBYNAME_R == 3

struct ghbnctx
  {
    int h_err;
    struct hostent_data hostent_data;
    struct hostent hostent;
  };

#else

struct ghbnctx
  {
    int h_err;
  };

#endif

struct hostent *gethostbyname_ctx (const char *host, struct ghbnctx *ctx);
int h_error_ctx (struct ghbnctx *ctx);
void free_ghbnctx (struct ghbnctx *ctx);

#endif

