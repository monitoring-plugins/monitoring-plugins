/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "check_dummy";
const char *revision = "$Revision$";
const char *copyright = "1999-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"

void print_help (void);
void print_usage (void);



int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (argc < 2)
		usage4 (_("Could not parse arguments"));
	else if (strcmp (argv[1], "-V") == 0 || strcmp (argv[1], "--version") == 0) {
		print_revision (progname, revision);
		exit (STATE_OK);
	}
	else if (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0) {
		print_help ();
		exit (STATE_OK);
	}
	else if (!is_integer (argv[1]))
		usage4 (_("Arguments to check_dummy must be an integer"));
	else
		result = atoi (argv[1]);

	switch (result) {
	case STATE_OK:
		printf (_("OK"));
		break;
	case STATE_WARNING:
		printf (_("WARNING"));
		break;
	case STATE_CRITICAL:
		printf (_("CRITICAL"));
		break;
	case STATE_UNKNOWN:
		printf (_("UNKNOWN"));
		break;
	default:
		printf (_("Status %d is not a supported error state\n"), result);
		break;
	}

	if (argc >= 3) 
		printf (": %s", argv[2]);

	printf("\n");

	return result;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\
This plugin will simply return the state corresponding to the numeric value\n\
of the <state> argument with optional text.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("Usage: %s <integer state> [optional text]\n", progname);
}
