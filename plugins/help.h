/*
 * help.h: custom help functions to print help text and XML
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

#ifndef NP_HELP_H
#define NP_HELP_H
/* Header file for nagios plugins help.c */

/* This file should be included in all plugins */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/*
 * The generic constants for XML
 */

#define XML_START \
	"<?xml version=\"1.0\"?>\n" \
	"<!DOCTYPE resource-agent SYSTEM \"ra-api-1.dtd\">\n" \
	"<resource-agent name=\"%s\">\n" \
	"<version>1.0</version>\n\n"
#define XML_ACTIONS \
	"<actions>\n" \
	"<action name=\"start\"   timeout=\"20\" />\n" \
	"<action name=\"stop\"    timeout=\"15\" />\n" \
	"<action name=\"status\"  timeout=\"20\" />\n" \
	"<action name=\"monitor\" timeout=\"20\" interval=\"60\" />\n" \
	"<action name=\"meta-data\"  timeout=\"15\" />\n" \
	"</actions>\n"
#define XML_END \
	"</resource-agent>\n"

/* <parameters>?</parameters> */
#define XML_PARAMETERS_BEGIN "<parameters>\n"
#define XML_PARAMETERS_END "</parameters>\n"

/* <parameter name="ipaddr" required="1" unique="1">
   <content type="string" default="value"/>
   ?
   </parameter>
 */
#define XML_PARAMETER_BEGIN \
	"<parameter name=\"%s\" unique=\"%d\" required=\"%d\">\n"
#define XML_PARAMETER_CONTENT \
	"<content type=\"%s\" default=\"%s\" />\n"
#define XML_PARAMETER_END "</parameter>\n"

/* <shortdesc lang="en">?</shortdesc> */
#define XML_SHORTDESC_BEGIN \
	"<shortdesc lang=\"%s\">"
#define XML_SHORTDESC_END "</shortdesc>\n"

/* <longdesc lang="en">?</longdesc> */
#define XML_LONGDESC_BEGIN \
	"<longdesc lang=\"%s\">\n"
#define XML_LONGDESC_END "</longdesc>\n"

#define LANG "en"

#define UT_METADATA _("\
 --metadata\n\
    Print resource agent meta-data.\n")

struct parameter_help {
	const char *name;
	int short_opt;
	const char *short_desc;
	int unique;
	int required;
	const char *type;
	const char *dflt_value;
	const char *value_desc;
	const char *long_desc;
};

struct help_head {
	const char *name;
	const char *short_desc;
	const char *long_desc;
};

void print_meta_data(struct help_head *, struct parameter_help *);
void print_parameters_help(struct parameter_help *);
void print_help_head(struct help_head *);

#endif /* NP_HELP_H */
