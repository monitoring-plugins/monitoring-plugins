/*
 *  This file is a ghastly hack because nobody can agree on
 *  gethostbyname_r()'s prototype.
 *
 *  Copyright (C) 2001,2002  Brian Stafford  <brian@stafford.uklinux.net>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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

