/*
 *  check_ide-smart v.1 - hacked version of ide-smart for Nagios
 *  Copyright (C) 2000 Robert Dale <rdale@digital-mission.com>
 *
 *  Nagios - http://www.nagios.org
 *
 *  Notes:
 *	   ide-smart has the same functionality as before. Some return
 *	   values were changed, otherwise the --nagios option was added.
 *
 *	   Run with:  check_ide-smart --nagios [-d] <DRIVE>
 *	   Where DRIVE is an IDE drive, ie. /dev/hda, /dev/hdb, /dev/hdc
 *
 *	     - Returns 0 on no errors
 *	     - Returns 1 on advisories
 *	     - Returns 2 on prefailure
 *	     - Returns -1 not too often
 *
 *  ide-smart 1.3 - IDE S.M.A.R.T. checking tool
 *  Copyright (C) 1998-1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>
 *		  1998	    Gadi Oxman <gadio@netvision.net.il>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 */

const char *progname = "check_ide_smart";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";
	
#include "common.h"
#include "utils.h"

void print_help (void);
void print_usage (void);

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/hdreg.h>
#include <linux/types.h>
#include <errno.h>
	
#define NR_ATTRIBUTES	30
	
#ifndef TRUE
#define TRUE 1
#endif	/*  */
	
#define PREFAILURE 2
#define ADVISORY 1
#define OPERATIONAL 0
#define UNKNOWN -1

typedef struct threshold_s
{
	__u8 id;
	__u8 threshold;
	__u8 reserved[10];
}
__attribute__ ((packed)) threshold_t;

typedef struct thresholds_s
{
	__u16 revision;
	threshold_t thresholds[NR_ATTRIBUTES];
	__u8 reserved[18];
	__u8 vendor[131];
	__u8 checksum;
}
__attribute__ ((packed)) thresholds_t;

typedef struct value_s
{
	__u8 id;
	__u16 status;
	__u8 value;
	__u8 vendor[8];
}
__attribute__ ((packed)) value_t;

typedef struct values_s
{
	__u16 revision;
	value_t values[NR_ATTRIBUTES];
	__u8 offline_status;
	__u8 vendor1;
	__u16 offline_timeout;
	__u8 vendor2;
	__u8 offline_capability;
	__u16 smart_capability;
	__u8 reserved[16];
	__u8 vendor[125];
	__u8 checksum;
}
__attribute__ ((packed)) values_t;

struct
{
	__u8 value;
	char *text;
}

offline_status_text[] =
	{
		{0x00, "NeverStarted"},
		{0x02, "Completed"},
		{0x04, "Suspended"},
		{0x05, "Aborted"},
		{0x06, "Failed"},
		{0, 0}
	};

struct
{
	__u8 value;
	char *text;
}

smart_command[] =
	{
		{SMART_ENABLE, "SMART_ENABLE"},
		{SMART_DISABLE, "SMART_DISABLE"},
		{SMART_IMMEDIATE_OFFLINE, "SMART_IMMEDIATE_OFFLINE"},
		{SMART_AUTO_OFFLINE, "SMART_AUTO_OFFLINE"}
	};


/* Index to smart_command table, keep in order */ 
enum SmartCommand 
	{ SMART_CMD_ENABLE,
		SMART_CMD_DISABLE,
		SMART_CMD_IMMEDIATE_OFFLINE,
		SMART_CMD_AUTO_OFFLINE 
	};

void print_values (values_t * p, thresholds_t * t);
int smart_cmd_simple (int fd, enum SmartCommand command, __u8 val0, char show_error); 

int
main (int argc, char *argv[]) 
{
	char *device = NULL;
	int command = -1;
	int o, longindex;
	int retval = 0;

	thresholds_t thresholds;
	values_t values;
	int fd;

	static struct option longopts[] = { 
		{"device", required_argument, 0, 'd'}, 
		{"immediate", no_argument, 0, 'i'}, 
		{"quiet-check", no_argument, 0, 'q'}, 
		{"auto-on", no_argument, 0, '1'}, 
		{"auto-off", no_argument, 0, '0'}, 
		{"nagios", no_argument, 0, 'n'}, 
		{"help", no_argument, 0, 'h'}, 
		{"version", no_argument, 0, 'V'}, {0, 0, 0, 0} 
	};

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	while (1) {
		
		o = getopt_long (argc, argv, "+d:iq10nhV", longopts, &longindex);

		switch (o) {
		case -1: 
								/* 
								 * bail out of the switch but not the loop, so
								 * that device can be extracted from argv.
								 */
			break;
		case 'd':
			device = optarg;
			break;
		case 'q':
			command = 3;
			break;
		case 'i':
			command = 2;
			break;
		case '1':
			command = 1;
			break;
		case '0':
			command = 0;
			break;
		case 'n':
			command = 4;
			break;
		case 'h':
			print_help ();
			return STATE_OK;
		case 'V':
			print_revision (progname, revision);
			return STATE_OK;
		default:
			usage2 (_("Unknown argument"), optarg);
		}
	}

	if (optind < argc) {
		device = argv[optind];
	}

	if (!device) {
		print_help ();
		return STATE_OK;
	}

	fd = open (device, O_RDONLY);

	if (fd < 0) {
		printf (_("CRITICAL - Couldn't open device %s: %s\n"), device, strerror (errno));
		return STATE_CRITICAL;
	}

	if (smart_cmd_simple (fd, SMART_CMD_ENABLE, 0, TRUE)) {
		printf (_("CRITICAL - SMART_CMD_ENABLE\n"));
		return STATE_CRITICAL;
	}

	switch (command) {
	case 0:
		retval = smart_cmd_simple (fd, SMART_CMD_AUTO_OFFLINE, 0, TRUE);
		break;
	case 1:
		retval = smart_cmd_simple (fd, SMART_CMD_AUTO_OFFLINE, 0xF8, TRUE);
		break;
	case 2:
		retval = smart_cmd_simple (fd, SMART_CMD_IMMEDIATE_OFFLINE, 0, TRUE);
		break;
	case 3:
		smart_read_values (fd, &values);
		smart_read_thresholds (fd, &thresholds);
		retval = values_not_passed (&values, &thresholds);
		break;
	case 4:
		smart_read_values (fd, &values);
		smart_read_thresholds (fd, &thresholds);
		retval = nagios (&values, &thresholds);
		break;
	default:
		smart_read_values (fd, &values);
		smart_read_thresholds (fd, &thresholds);
		print_values (&values, &thresholds);
		break;
	}
	close (fd);
	return retval;
}



char *
get_offline_text (int status) 
{
	int i;
	for (i = 0; offline_status_text[i].text; i++) {
		if (offline_status_text[i].value == status) {
			return offline_status_text[i].text;
		}
	}
	return "UNKNOW";
}



int
smart_read_values (int fd, values_t * values) 
{
	int e;
	__u8 args[4 + 512];
	args[0] = WIN_SMART;
	args[1] = 0;
	args[2] = SMART_READ_VALUES;
	args[3] = 1;
	if (ioctl (fd, HDIO_DRIVE_CMD, &args)) {
		e = errno;
		printf (_("CRITICAL - SMART_READ_VALUES: %s\n"), strerror (errno));
		return e;
	}
	memcpy (values, args + 4, 512);
	return 0;
}



int
values_not_passed (values_t * p, thresholds_t * t) 
{
	value_t * value = p->values;
	threshold_t * threshold = t->thresholds;
	int failed = 0;
	int passed = 0;
	int i;
	for (i = 0; i < NR_ATTRIBUTES; i++) {
		if (value->id && threshold->id && value->id == threshold->id) {
			if (value->value <= threshold->threshold) {
				++failed;
			}
			else {
				++passed;
			}
		}
		++value;
		++threshold;
	}
	return (passed ? -failed : 2);
}



int
nagios (values_t * p, thresholds_t * t) 
{
	value_t * value = p->values;
	threshold_t * threshold = t->thresholds;
	int status = OPERATIONAL;
	int prefailure = 0;
	int advisory = 0;
	int failed = 0;
	int passed = 0;
	int total = 0;
	int i;
	for (i = 0; i < NR_ATTRIBUTES; i++) {
		if (value->id && threshold->id && value->id == threshold->id) {
			if (value->value <= threshold->threshold) {
				++failed;
				if (value->status & 1) {
					status = PREFAILURE;
					++prefailure;
				}
				else {
					status = ADVISORY;
					++advisory;
				}
			}
			else {
				++passed;
			}
			++total;
		}
		++value;
		++threshold;
	}
	switch (status) {
	case PREFAILURE:
		printf (_("CRITICAL - %d Harddrive PreFailure%cDetected! %d/%d tests failed.\n"),
		        prefailure,
		        prefailure > 1 ? 's' : ' ',
		        failed,
	          total);
		status=STATE_CRITICAL;
		break;
	case ADVISORY:
		printf (_("WARNING - %d Harddrive Advisor%s Detected. %d/%d tests failed.\n"),
		        advisory,
		        advisory > 1 ? "ies" : "y",
		        failed,
		        total);
		status=STATE_WARNING;
		break;
	case OPERATIONAL:
		printf (_("OK - Operational (%d/%d tests passed)\n"), passed, total);
		status=STATE_OK;
		break;
	default:
		printf (_("ERROR - Status '%d' unkown. %d/%d tests passed\n"), status,
						passed, total);
		status = STATE_UNKNOWN;
		break;
	}
	return status;
}



void
print_value (value_t * p, threshold_t * t) 
{
	printf ("Id=%3d, Status=%2d {%s , %s}, Value=%3d, Threshold=%3d, %s\n",
					p->id, p->status, p->status & 1 ? "PreFailure" : "Advisory   ",
					p->status & 2 ? "OnLine " : "OffLine", p->value, t->threshold,
					p->value > t->threshold ? "Passed" : "Failed");
}



void
print_values (values_t * p, thresholds_t * t)
{
	value_t * value = p->values;
	threshold_t * threshold = t->thresholds;
	int i;
	for (i = 0; i < NR_ATTRIBUTES; i++) {
		if (value->id && threshold->id && value->id == threshold->id) {
			print_value (value++, threshold++);
		}
	}
	printf
		(_("OffLineStatus=%d {%s}, AutoOffLine=%s, OffLineTimeout=%d minutes\n"),
		 p->offline_status,
		 get_offline_text (p->offline_status & 0x7f),
		 (p->offline_status & 0x80 ? "Yes" : "No"),
		 p->offline_timeout / 60);
	printf
		(_("OffLineCapability=%d {%s %s %s}\n"),
		 p->offline_capability,
		 p->offline_capability & 1 ? "Immediate" : "",
		 p->offline_capability & 2 ? "Auto" : "",
		 p->offline_capability & 4 ? "AbortOnCmd" : "SuspendOnCmd");
	printf
		(_("SmartRevision=%d, CheckSum=%d, SmartCapability=%d {%s %s}\n"),
		 p->revision,
		 p->checksum,
		 p->smart_capability,
		 p->smart_capability & 1 ? "SaveOnStandBy" : "",
		 p->smart_capability & 2 ? "AutoSave" : "");
}



void
print_thresholds (thresholds_t * p) 
{
	threshold_t * threshold = p->thresholds;
	int i;
	printf ("\n");
	printf ("SmartRevision=%d\n", p->revision);
	for (i = 0; i < NR_ATTRIBUTES; i++) {
		if (threshold->id) {
			printf ("Id=%3d, Threshold=%3d\n", threshold->id,
							threshold->threshold); }
		++threshold;
	}
	printf ("CheckSum=%d\n", p->checksum);
}

int
smart_cmd_simple (int fd, enum SmartCommand command, __u8 val0, char show_error) 
{
	int e = 0;
	__u8 args[4];
	args[0] = WIN_SMART;
	args[1] = val0;
	args[2] = smart_command[command].value;
	args[3] = 0;
	if (ioctl (fd, HDIO_DRIVE_CMD, &args)) {
		e = errno;
		if (show_error) {
			printf (_("CRITICAL - %s: %s\n"), smart_command[command].text, strerror (errno));
		}
	}
	return e;
}



int
smart_read_thresholds (int fd, thresholds_t * thresholds) 
{
	int e;
	__u8 args[4 + 512];
	args[0] = WIN_SMART;
  args[1] = 0;
  args[2] = SMART_READ_THRESHOLDS;
  args[3] = 1;
	if (ioctl (fd, HDIO_DRIVE_CMD, &args)) {
		e = errno;
		printf (_("CRITICAL - SMART_READ_THRESHOLDS: %s\n"), strerror (errno));
		return e;
	}
	memcpy (thresholds, args + 4, 512);
	return 0;
}


void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Nagios feature - 1999 Robert Dale <rdale@digital-mission.com>\n");
	printf ("(C) 1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf(_("This plugin checks a local hard drive with the (Linux specific) SMART interface [http://smartlinux.sourceforge.net/smart/index.php].\n\n"));
	
	printf ("\
Usage: %s [OPTION] [DEVICE]\n\
 -d, --device=DEVICE\n\
    Select device DEVICE\n\
    Note: if the device is selected with this option, _no_ other options are accepted\n\
 -i, --immediate\n\
    Perform immediately offline tests\n\
 -q, --quiet-check\n\
    Returns the number of failed tests\n\
 -1, --auto-on\n\
    Turn on automatic offline tests\n\
 -0, --auto-off\n\
    Turn off automatic offline tests\n\
 -n, --nagios\n\
    Output suitable for Nagios\n", progname);
}


void
print_usage (void)
{
	printf ("\
Usage: %s [-d <device>] [-i <immediate>] [-q quiet] [-1 <auto-on>]\n\
                        [-O <auto-off>] [-n <nagios>]\n", progname);
}
