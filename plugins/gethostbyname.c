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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _SVID_SOURCE	1	/* Need this to get gethostbyname_r() */

#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

#include "gethostbyname.h"

#if HAVE_GETIPNODEBYNAME

void
free_ghbnctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  if (ctx->hostent != NULL)
    freehostent (ctx->hostent);
}

struct hostent *
gethostbyname_ctx (const char *host, struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  memset (ctx, 0, sizeof (struct ghbnctx));
  ctx->hostent = getipnodebyname (host, AF_UNSPEC, AI_ADDRCONFIG, &ctx->h_err);
  return ctx->hostent;
}

int
h_error_ctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  return ctx->h_err;
}

#elif HAVE_GETHOSTBYNAME_R == 6

void
free_ghbnctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  if (ctx->hostbuf != NULL)
    free (ctx->hostbuf);
}

struct hostent *
gethostbyname_ctx (const char *host, struct ghbnctx *ctx)
{
  struct hostent *hp;
  char *tmp;
  int err;

  assert (ctx != NULL);

  memset (ctx, 0, sizeof (struct ghbnctx));
  ctx->hostbuf_len = 2048;
  if ((ctx->hostbuf = malloc (ctx->hostbuf_len)) == NULL)
    {
      errno = ENOMEM;
      return NULL;
    }
  while ((err = gethostbyname_r (host,
  				 &ctx->hostent, ctx->hostbuf, ctx->hostbuf_len,
  				 &hp, &ctx->h_err)) == ERANGE)
    {
      ctx->hostbuf_len += 1024;
      if ((tmp = realloc (ctx->hostbuf, ctx->hostbuf_len)) == NULL)
	{
	  errno = ENOMEM;
	  return NULL;
	}
      ctx->hostbuf = tmp;
    }
  if (err != 0)
    {
      errno = err;
      return NULL;
    }
  return hp;
}

int
h_error_ctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  return ctx->h_err;
}

#elif HAVE_GETHOSTBYNAME_R == 5

void
free_ghbnctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  if (ctx->hostbuf != NULL)
    free (ctx->hostbuf);
}

struct hostent *
gethostbyname_ctx (const char *host, struct ghbnctx *ctx)
{
  struct hostent *hp;
  char *tmp;

  assert (ctx != NULL);

  memset (ctx, 0, sizeof (struct ghbnctx));
  ctx->hostbuf_len = 2048;
  if ((ctx->hostbuf = malloc (ctx->hostbuf_len)) == NULL)
    {
      errno = ENOMEM;
      return NULL;
    }
  while ((hp = gethostbyname_r (host, &ctx->hostent,
  				ctx->hostbuf, ctx->hostbuf_len,
  				&ctx->h_err)) == NULL && errno == ERANGE)
    {
      ctx->hostbuf_len += 1024;
      if ((tmp = realloc (ctx->hostbuf, ctx->hostbuf_len)) == NULL)
	{
	  errno = ENOMEM;
	  return NULL;
	}
      ctx->hostbuf = tmp;
    }
  return hp;
}

int
h_error_ctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  return ctx->h_err;
}

#elif HAVE_GETHOSTBYNAME_R == 3

void
free_ghbnctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  /* FIXME: does this need to do anything? */
}

struct hostent *
gethostbyname_ctx (const char *host, struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  if (!gethostbyname_r (host, &ctx->hostent, &ctx->hostent_data))
    {
      ctx->h_err = h_errno;	/* FIXME: is this correct? */
      return NULL;
    }
  return &ctx->hostent;
}
  
int
h_error_ctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  return ctx->h_err;
}

#else

void
free_ghbnctx (struct ghbnctx *ctx __attribute__ ((unused)))
{
  assert (ctx != NULL);
}

struct hostent *
gethostbyname_ctx (const char *host, struct ghbnctx *ctx)
{
  struct hostent *hp;

  hp = gethostbyname (host);
  if (hp == NULL)
    ctx->h_err = h_errno;
  return hp;
}

int
h_error_ctx (struct ghbnctx *ctx)
{
  assert (ctx != NULL);

  return ctx->h_err;
}

#endif
