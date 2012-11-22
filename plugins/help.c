/*
 * help.c: custom help functions to print help text and XML
 * meta-data
 *
 * Copyright (C) 2012 Dejan Muhamedagic <dejan@hello-penguin.com>
 *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "common.h"
#include "help.h"

static void meta_long_desc_from_short(const char *, const char *);
static void meta_long_desc(const char *, const char *);
static void meta_short_desc(const char *, const char *);
static void meta_param(const char *, struct parameter_help *);
static void indent(const char *, const char *);

static void
meta_long_desc_from_short(const char *lang, const char *s)
{
	printf(XML_LONGDESC_BEGIN, lang);
	printf("%s\n", _(s));
	printf(XML_LONGDESC_END);
}

static void
meta_long_desc(const char *lang, const char *s)
{
	printf(XML_LONGDESC_BEGIN, lang);
	printf("%s", _(s));
	printf(XML_LONGDESC_END);
}

static void
meta_short_desc(const char *lang, const char *s)
{
	printf(XML_SHORTDESC_BEGIN, lang);
	printf("%s", _(s));
	printf(XML_SHORTDESC_END);
}

static void
meta_param(const char *lang, struct parameter_help *ph)
{
	printf(XML_PARAMETER_BEGIN, ph->name, ph->unique, ph->required);
	if (ph->long_desc) {
		meta_long_desc(lang, ph->long_desc);
	} else {
		meta_long_desc_from_short(lang, ph->short_desc);
	}
	meta_short_desc(lang, ph->short_desc);
	printf(XML_PARAMETER_CONTENT, ph->type, ph->dflt_value);
	printf(XML_PARAMETER_END);
}

void
print_meta_data(struct help_head *hh, struct parameter_help *ph)
{
	struct parameter_help *p;

	printf(XML_START, hh->name);
	if (hh->long_desc) {
		meta_long_desc(LANG, hh->long_desc);
	} else {
		meta_long_desc_from_short(LANG, hh->short_desc);
	}
	meta_short_desc(LANG, hh->short_desc);
	printf("\n");
	printf(XML_PARAMETERS_BEGIN);
	printf("\n");
	for (p = ph; p->short_desc; p++) {
		meta_param(LANG, p);
		printf("\n");
	}
	printf(XML_PARAMETERS_END);
	printf("\n");
	printf(XML_ACTIONS);
	printf("\n");
	printf(XML_END);
}

static void
indent(const char *s, const char *tab)
{
	const char *p, *q;
	int len;

	if (!s )
		return;
	for (p = s; p < s+strlen(s) && *p; p = q+1) {
		q = strchr(p, '\n');
		if( q ) {
			printf("%s%.*s", tab, q-p+1, p);
		} else {
			printf("%s%s\n", tab, p);
		}
	}
}

void
print_help_head(struct help_head *hh)
{
	if (hh->long_desc) {
		printf(_(hh->long_desc));
	} else {
		printf("%s\n", _(hh->short_desc));
	}
}

void
print_parameters_help(struct parameter_help *ph)
{
	struct parameter_help *p;

	for (p = ph; p->short_desc; p++) {
		if (p->short_opt) {
			printf(" -%c, --%s=%s\n", (unsigned char)(p->short_opt),
				p->name, p->value_desc);
		} else {
			printf(" --%s=%s\n", p->name, p->value_desc);
		}
		indent(_(p->long_desc), "    ");
		printf(UT_METADATA);
	}
}
