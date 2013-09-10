/*****************************************************************************
* 
* Nagios check_ide_smart plugin
* ide-smart 1.3 - IDE S.M.A.R.T. checking tool
* 
* License: GPL
* Copyright (C) 1998-1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>
*               1998      Gadi Oxman <gadio@netvision.net.il>
* Copyright (c) 2000 Robert Dale <rdale@digital-mission.com>
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_ide_smart plugin
* 
* This plugin checks a local hard drive with the (Linux specific) SMART
* interface
* 
* 
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
* 
*****************************************************************************/

const char *progname = "check_ide_smart";
const char *copyright = "1998-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";
	
#include "common.h"
#include "utils.h"

void print_help (void);
void print_usage (void);

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/hdreg.h>
#include <linux/types.h>

#define OPEN_MODE O_RDONLY
#endif /* __linux__ */
#ifdef __NetBSD__
#include <sys/device.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/videoio.h> /* for __u8 and friends */
#include <sys/scsiio.h>
#include <sys/ataio.h>
#include <dev/ata/atareg.h>
#include <dev/ic/wdcreg.h>

#define SMART_ENABLE WDSM_ENABLE_OPS
#define SMART_DISABLE WDSM_DISABLE_OPS
#define SMART_IMMEDIATE_OFFLINE WDSM_EXEC_OFFL_IMM
#define SMART_AUTO_OFFLINE 0xdb /* undefined in NetBSD headers */

#define OPEN_MODE O_RDWR
#endif /* __NetBSD__ */
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

char *get_offline_text (int);
int smart_read_values (int, values_t *);
int values_not_passed (values_t *, thresholds_t *);
int nagios (values_t *, thresholds_t *);
void print_value (value_t *, threshold_t *);
void print_values (values_t *, thresholds_t *);
int smart_cmd_simple (int, enum SmartCommand, __u8, char);
int smart_read_thresholds (int, thresholds_t *);

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
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0}
	};

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	while (1) {
		
		o = getopt_long (argc, argv, "+d:iq10nhV", longopts, &longindex);

		if (o == -1 || o == EOF || o == 1)
			break;

		switch (o) {
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
			print_revision (progname, NP_VERSION);
			return STATE_OK;
		default:
			usage5 ();
		}
	}

	if (optind < argc) {
		device = argv[optind];
	}

	if (!device) {
		print_help ();
		return STATE_OK;
	}

	fd = open (device, OPEN_MODE);

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
#ifdef __linux__
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
#endif /* __linux__ */
#ifdef __NetBSD__
	struct atareq req;
	unsigned char inbuf[DEV_BSIZE];

	memset(&req, 0, sizeof(req));
	req.timeout = 1000;
	memset(&inbuf, 0, sizeof(inbuf));

	req.flags = ATACMD_READ;
	req.features = WDSM_RD_DATA;
	req.command = WDCC_SMART;
	req.databuf = (char *)inbuf;
	req.datalen = sizeof(inbuf);
	req.cylinder = WDSMART_CYL;

	if (ioctl(fd, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK)
			errno = ENODEV;
	}

	if (errno != 0) {
		int e = errno;
		printf (_("CRITICAL - SMART_READ_VALUES: %s\n"), strerror (errno));
		return e;
	}

	(void)memcpy(values, inbuf, 512);
#endif /* __NetBSD__ */
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


int
smart_cmd_simple (int fd, enum SmartCommand command, __u8 val0, char show_error) 
{
	int e = 0;
#ifdef __linux__
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
#endif /* __linux__ */
#ifdef __NetBSD__
	struct atareq req;

	memset(&req, 0, sizeof(req));
	req.timeout = 1000;
	req.flags = ATACMD_READREG;
	req.features = smart_command[command].value;
	req.command = WDCC_SMART;
	req.cylinder = WDSMART_CYL;
	req.sec_count = val0;

	if (ioctl(fd, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK)
			errno = ENODEV;
		if (req.cylinder != WDSMART_CYL)
			errno = ENODEV;
	}

	if (errno != 0) {
		e = errno;
		printf (_("CRITICAL - %s: %s\n"), smart_command[command].text, strerror (errno));
		return e;
	}
#endif /* __NetBSD__ */
	return e;
}



int
smart_read_thresholds (int fd, thresholds_t * thresholds) 
{
#ifdef __linux__
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
#endif /* __linux__ */
#ifdef __NetBSD__
	struct atareq req;
	unsigned char inbuf[DEV_BSIZE];

	memset(&req, 0, sizeof(req));
	req.timeout = 1000;
	memset(&inbuf, 0, sizeof(inbuf));

	req.flags = ATACMD_READ;
	req.features = WDSM_RD_THRESHOLDS;
	req.command = WDCC_SMART;
	req.databuf = (char *)inbuf;
	req.datalen = sizeof(inbuf);
	req.cylinder = WDSMART_CYL;

	if (ioctl(fd, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK)
			errno = ENODEV;
	}

	if (errno != 0) {
		int e = errno;
		printf (_("CRITICAL - SMART_READ_THRESHOLDS: %s\n"), strerror (errno));
		return e;
	}

	(void)memcpy(thresholds, inbuf, 512);
#endif /* __NetBSD__ */
	return 0;
}


void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Nagios feature - 1999 Robert Dale <rdale@digital-mission.com>\n");
	printf ("(C) 1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin checks a local hard drive with the (Linux specific) SMART interface [http://smartlinux.sourceforge.net/smart/index.php]."));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (" %s\n", "-d, --device=DEVICE");
  printf ("    %s\n", _("Select device DEVICE"));
  printf ("    %s\n", _("Note: if the device is selected with this option, _no_ other options are accepted"));
  printf (" %s\n", "-i, --immediate");
  printf ("    %s\n", _("Perform immediately offline tests"));
  printf (" %s\n", "-q, --quiet-check");
  printf ("    %s\n", _("Returns the number of failed tests"));
  printf (" %s\n", "-1, --auto-on");
  printf ("    %s\n", _("Turn on automatic offline tests"));
  printf (" %s\n", "-0, --auto-off");
  printf ("    %s\n", _("Turn off automatic offline tests"));
  printf (" %s\n", "-n, --nagios");
  printf ("    %s\n", _("Output suitable for Nagios"));

  printf (UT_SUPPORT);
}

 /* todo : add to the long nanual as example
 *
 *     Run with:  check_ide-smart --nagios [-d] <DRIVE>
 *     Where DRIVE is an IDE drive, ie. /dev/hda, /dev/hdb, /dev/hdc
 *
 *       - Returns 0 on no errors
 *       - Returns 1 on advisories
 *       - Returns 2 on prefailure
 *       - Returns -1 not too often
 */


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf ("%s [-d <device>] [-i <immediate>] [-q quiet] [-1 <auto-on>]",progname);
  printf (" [-O <auto-off>] [-n <nagios>]\n");
}
