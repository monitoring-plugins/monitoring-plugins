/*****************************************************************************
 *
 * Monitoring check_ide_smart plugin
 * ide-smart 1.3 - IDE S.M.A.R.T. checking tool
 *
 * License: GPL
 * Copyright (C) 1998-1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>
 *               1998      Gadi Oxman <gadio@netvision.net.il>
 * Copyright (c) 2000 Robert Dale <rdale@digital-mission.com>
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
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
const char *copyright = "1998-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "check_ide_smart.d/config.h"
#include "states.h"

static void print_help(void);
void print_usage(void);

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#ifdef __linux__
#	include <linux/hdreg.h>
#	include <linux/types.h>

#	define OPEN_MODE O_RDONLY
#endif /* __linux__ */
#ifdef __NetBSD__
#	include <sys/device.h>
#	include <sys/param.h>
#	include <sys/sysctl.h>
#	include <sys/scsiio.h>
#	include <sys/ataio.h>
#	include <dev/ata/atareg.h>
#	include <dev/ic/wdcreg.h>

#	define SMART_ENABLE            WDSM_ENABLE_OPS
#	define SMART_DISABLE           WDSM_DISABLE_OPS
#	define SMART_IMMEDIATE_OFFLINE WDSM_EXEC_OFFL_IMM
#	define SMART_AUTO_OFFLINE      0xdb /* undefined in NetBSD headers */

#	define OPEN_MODE O_RDWR
#endif /* __NetBSD__ */
#include <errno.h>

#define NR_ATTRIBUTES 30

#define PREFAILURE  2
#define ADVISORY    1
#define OPERATIONAL 0
#define UNKNOWN     -1

typedef struct {
	uint8_t id;
	uint8_t threshold;
	uint8_t reserved[10];
} __attribute__((packed)) smart_threshold;

typedef struct {
	uint16_t revision;
	smart_threshold thresholds[NR_ATTRIBUTES];
	uint8_t reserved[18];
	uint8_t vendor[131];
	uint8_t checksum;
} __attribute__((packed)) smart_thresholds;

typedef struct {
	uint8_t id;
	uint16_t status;
	uint8_t value;
	uint8_t vendor[8];
} __attribute__((packed)) smart_value;

typedef struct {
	uint16_t revision;
	smart_value values[NR_ATTRIBUTES];
	uint8_t offline_status;
	uint8_t vendor1;
	uint16_t offline_timeout;
	uint8_t vendor2;
	uint8_t offline_capability;
	uint16_t smart_capability;
	uint8_t reserved[16];
	uint8_t vendor[125];
	uint8_t checksum;
} __attribute__((packed)) smart_values;

static struct {
	uint8_t value;
	char *text;
} offline_status_text[] = {{0x00, "NeverStarted"}, {0x02, "Completed"}, {0x04, "Suspended"}, {0x05, "Aborted"}, {0x06, "Failed"}, {0, 0}};

static struct {
	uint8_t value;
	char *text;
} smart_command[] = {{SMART_ENABLE, "SMART_ENABLE"},
					 {SMART_DISABLE, "SMART_DISABLE"},
					 {SMART_IMMEDIATE_OFFLINE, "SMART_IMMEDIATE_OFFLINE"},
					 {SMART_AUTO_OFFLINE, "SMART_AUTO_OFFLINE"}};

/* Index to smart_command table, keep in order */
enum SmartCommand {
	SMART_CMD_ENABLE,
	SMART_CMD_DISABLE,
	SMART_CMD_IMMEDIATE_OFFLINE,
	SMART_CMD_AUTO_OFFLINE
};

static char *get_offline_text(int /*status*/);
static int smart_read_values(int /*fd*/, smart_values * /*values*/);
static mp_state_enum compare_values_and_thresholds(smart_values * /*p*/, smart_thresholds * /*t*/);
static void print_value(smart_value * /*p*/, smart_threshold * /*t*/);
static void print_values(smart_values * /*p*/, smart_thresholds * /*t*/);
static mp_state_enum smart_cmd_simple(int /*fd*/, enum SmartCommand /*command*/, uint8_t /*val0*/, bool /*show_error*/);
static int smart_read_thresholds(int /*fd*/, smart_thresholds * /*thresholds*/);
static int verbose = 0;

typedef struct {
	int errorcode;
	check_ide_smart_config config;
} check_ide_smart_config_wrapper;
static check_ide_smart_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"device", required_argument, 0, 'd'},
									   {"immediate", no_argument, 0, 'i'},
									   {"quiet-check", no_argument, 0, 'q'},
									   {"auto-on", no_argument, 0, '1'},
									   {"auto-off", no_argument, 0, '0'},
									   {"nagios", no_argument, 0, 'n'}, /* DEPRECATED, but we still accept it */
									   {"help", no_argument, 0, 'h'},
									   {"version", no_argument, 0, 'V'},
									   {0, 0, 0, 0}};

	check_ide_smart_config_wrapper result = {
		.errorcode = OK,
		.config = check_ide_smart_init(),
	};

	while (true) {
		int longindex = 0;
		int option_index = getopt_long(argc, argv, "+d:iq10nhVv", longopts, &longindex);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case 'd':
			result.config.device = optarg;
			break;
		case 'q':
			fprintf(stderr, "%s\n", _("DEPRECATION WARNING: the -q switch (quiet output) is no longer \"quiet\"."));
			fprintf(stderr, "%s\n", _("Nagios-compatible output is now always returned."));
			break;
		case 'i':
		case '1':
		case '0':
			printf("%s\n", _("SMART commands are broken and have been disabled (See Notes in --help)."));
			result.errorcode = ERROR;
			return result;
			break;
		case 'n':
			fprintf(stderr, "%s\n", _("DEPRECATION WARNING: the -n switch (Nagios-compatible output) is now the"));
			fprintf(stderr, "%s\n", _("default and will be removed from future releases."));
			break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'h':
			print_help();
			exit(STATE_UNKNOWN);
		case 'V':
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		default:
			usage5();
		}
	}

	if (optind < argc) {
		result.config.device = argv[optind];
	}

	if (result.config.device == NULL) {
		print_help();
		exit(STATE_UNKNOWN);
	}

	return result;
}

int main(int argc, char *argv[]) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_ide_smart_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode != OK) {
		die(STATE_UNKNOWN, _("Failed to parse commandline"));
	}

	check_ide_smart_config config = tmp_config.config;

	int device_file_descriptor = open(config.device, OPEN_MODE);

	if (device_file_descriptor < 0) {
		printf(_("CRITICAL - Couldn't open device %s: %s\n"), config.device, strerror(errno));
		return STATE_CRITICAL;
	}

	if (smart_cmd_simple(device_file_descriptor, SMART_CMD_ENABLE, 0, false)) {
		printf(_("CRITICAL - SMART_CMD_ENABLE\n"));
		return STATE_CRITICAL;
	}

	smart_values values;
	smart_read_values(device_file_descriptor, &values);
	smart_thresholds thresholds;
	smart_read_thresholds(device_file_descriptor, &thresholds);
	mp_state_enum retval = compare_values_and_thresholds(&values, &thresholds);
	if (verbose) {
		print_values(&values, &thresholds);
	}

	close(device_file_descriptor);
	return retval;
}

char *get_offline_text(int status) {
	for (int index = 0; offline_status_text[index].text; index++) {
		if (offline_status_text[index].value == status) {
			return offline_status_text[index].text;
		}
	}
	return "UNKNOWN";
}

int smart_read_values(int file_descriptor, smart_values *values) {
#ifdef __linux__
	uint8_t args[4 + 512];
	args[0] = WIN_SMART;
	args[1] = 0;
	args[2] = SMART_READ_VALUES;
	args[3] = 1;
	if (ioctl(file_descriptor, HDIO_DRIVE_CMD, &args)) {
		int errno_storage = errno;
		printf(_("CRITICAL - SMART_READ_VALUES: %s\n"), strerror(errno));
		return errno_storage;
	}
	memcpy(values, args + 4, 512);
#elif defined __NetBSD__
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

	if (ioctl(file_descriptor, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK) {
			errno = ENODEV;
		}
	}

	if (errno != 0) {
		int errno_storage = errno;
		printf(_("CRITICAL - SMART_READ_VALUES: %s\n"), strerror(errno));
		return errno_storage;
	}

	(void)memcpy(values, inbuf, 512);
#else // __linux__ || __NetBSD__
#	error Not implemented for this OS
#endif

	return 0;
}

mp_state_enum compare_values_and_thresholds(smart_values *values, smart_thresholds *thresholds) {
	smart_value *value = values->values;
	smart_threshold *threshold = thresholds->thresholds;

	int status = OPERATIONAL;
	int prefailure = 0;
	int advisory = 0;
	int failed = 0;
	int passed = 0;
	int total = 0;
	for (int i = 0; i < NR_ATTRIBUTES; i++) {
		if (value->id && threshold->id && value->id == threshold->id) {
			if (value->value < threshold->threshold) {
				++failed;
				if (value->status & 1) {
					status = PREFAILURE;
					++prefailure;
				} else {
					status = ADVISORY;
					++advisory;
				}
			} else {
				++passed;
			}
			++total;
		}
		++value;
		++threshold;
	}

	switch (status) {
	case PREFAILURE:
		printf(_("CRITICAL - %d Harddrive PreFailure%cDetected! %d/%d tests failed.\n"), prefailure, prefailure > 1 ? 's' : ' ', failed,
			   total);
		status = STATE_CRITICAL;
		break;
	case ADVISORY:
		printf(_("WARNING - %d Harddrive Advisor%s Detected. %d/%d tests failed.\n"), advisory, advisory > 1 ? "ies" : "y", failed, total);
		status = STATE_WARNING;
		break;
	case OPERATIONAL:
		printf(_("OK - Operational (%d/%d tests passed)\n"), passed, total);
		status = STATE_OK;
		break;
	default:
		printf(_("ERROR - Status '%d' unknown. %d/%d tests passed\n"), status, passed, total);
		status = STATE_UNKNOWN;
		break;
	}
	return status;
}

void print_value(smart_value *value_pointer, smart_threshold *threshold_pointer) {
	printf("Id=%3d, Status=%2d {%s , %s}, Value=%3d, Threshold=%3d, %s\n", value_pointer->id, value_pointer->status,
		   value_pointer->status & 1 ? "PreFailure" : "Advisory   ", value_pointer->status & 2 ? "OnLine " : "OffLine",
		   value_pointer->value, threshold_pointer->threshold, value_pointer->value >= threshold_pointer->threshold ? "Passed" : "Failed");
}

void print_values(smart_values *values, smart_thresholds *thresholds) {
	smart_value *value = values->values;
	smart_threshold *threshold = thresholds->thresholds;
	for (int i = 0; i < NR_ATTRIBUTES; i++) {
		if (value->id && threshold->id && value->id == threshold->id) {
			print_value(value++, threshold++);
		}
	}
	printf(_("OffLineStatus=%d {%s}, AutoOffLine=%s, OffLineTimeout=%d minutes\n"), values->offline_status,
		   get_offline_text(values->offline_status & 0x7f), (values->offline_status & 0x80 ? "Yes" : "No"), values->offline_timeout / 60);
	printf(_("OffLineCapability=%d {%s %s %s}\n"), values->offline_capability, values->offline_capability & 1 ? "Immediate" : "",
		   values->offline_capability & 2 ? "Auto" : "", values->offline_capability & 4 ? "AbortOnCmd" : "SuspendOnCmd");
	printf(_("SmartRevision=%d, CheckSum=%d, SmartCapability=%d {%s %s}\n"), values->revision, values->checksum, values->smart_capability,
		   values->smart_capability & 1 ? "SaveOnStandBy" : "", values->smart_capability & 2 ? "AutoSave" : "");
}

mp_state_enum smart_cmd_simple(int file_descriptor, enum SmartCommand command, uint8_t val0, bool show_error) {
	mp_state_enum result = STATE_UNKNOWN;
#ifdef __linux__
	uint8_t args[4] = {
		WIN_SMART,
		val0,
		smart_command[command].value,
		0,
	};

	if (ioctl(file_descriptor, HDIO_DRIVE_CMD, &args)) {
		result = STATE_CRITICAL;
		if (show_error) {
			printf(_("CRITICAL - %s: %s\n"), smart_command[command].text, strerror(errno));
		}
	} else {
		result = STATE_OK;
		if (show_error) {
			printf(_("OK - Command sent (%s)\n"), smart_command[command].text);
		}
	}

#elif defined __NetBSD__
	struct atareq req;

	memset(&req, 0, sizeof(req));
	req.timeout = 1000;
	req.flags = ATACMD_READREG;
	req.features = smart_command[command].value;
	req.command = WDCC_SMART;
	req.cylinder = WDSMART_CYL;
	req.sec_count = val0;

	if (ioctl(file_descriptor, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK) {
			errno = ENODEV;
		}
		if (req.cylinder != WDSMART_CYL) {
			errno = ENODEV;
		}
	}

	if (errno != 0) {
		result = STATE_CRITICAL;
		if (show_error) {
			printf(_("CRITICAL - %s: %s\n"), smart_command[command].text, strerror(errno));
		}
	} else {
		result = STATE_OK;
		if (show_error) {
			printf(_("OK - Command sent (%s)\n"), smart_command[command].text);
		}
	}
#else
#	error Not implemented for this OS
#endif /* __NetBSD__ */

	return result;
}

int smart_read_thresholds(int file_descriptor, smart_thresholds *thresholds) {
#ifdef __linux__
	uint8_t args[4 + 512];
	args[0] = WIN_SMART;
	args[1] = 0;
	args[2] = SMART_READ_THRESHOLDS;
	args[3] = 1;
	if (ioctl(file_descriptor, HDIO_DRIVE_CMD, &args)) {
		int errno_storage = errno;
		printf(_("CRITICAL - SMART_READ_THRESHOLDS: %s\n"), strerror(errno));
		return errno_storage;
	}
	memcpy(thresholds, args + 4, 512);
#elif defined __NetBSD__
	struct atareq req;
	memset(&req, 0, sizeof(req));
	req.timeout = 1000;

	unsigned char inbuf[DEV_BSIZE];
	memset(&inbuf, 0, sizeof(inbuf));

	req.flags = ATACMD_READ;
	req.features = WDSM_RD_THRESHOLDS;
	req.command = WDCC_SMART;
	req.databuf = (char *)inbuf;
	req.datalen = sizeof(inbuf);
	req.cylinder = WDSMART_CYL;

	if (ioctl(file_descriptor, ATAIOCCOMMAND, &req) == 0) {
		if (req.retsts != ATACMD_OK) {
			errno = ENODEV;
		}
	}

	if (errno != 0) {
		int errno_storage = errno;
		printf(_("CRITICAL - SMART_READ_THRESHOLDS: %s\n"), strerror(errno));
		return errno_storage;
	}

	(void)memcpy(thresholds, inbuf, 512);
#else
#	error Not implemented for this OS
#endif /* __NetBSD__ */

	return 0;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("(C) 1999 Ragnar Hojland Espinosa <ragnar@lightside.dhis.org>\n");
	printf("Plugin implementation - 1999 Robert Dale <rdale@digital-mission.com>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin checks a local hard drive with the (Linux specific) SMART interface "
			 "[http://smartlinux.sourceforge.net/smart/index.php]."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-d, --device=DEVICE");
	printf("    %s\n", _("Select device DEVICE"));
	printf("    %s\n", _("Note: if the device is specified without this option, any further option will"));
	printf("          %s\n", _("be ignored."));

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("The SMART command modes (-i/--immediate, -0/--auto-off and -1/--auto-on) were"));
	printf(" %s\n", _("broken in an underhand manner and have been disabled. You can use smartctl"));
	printf(" %s\n", _("instead:"));
	printf("  %s\n", _("-0/--auto-off:  use \"smartctl --offlineauto=off\""));
	printf("  %s\n", _("-1/--auto-on:   use \"smartctl --offlineauto=on\""));
	printf("  %s\n", _("-i/--immediate: use \"smartctl --test=offline\""));

	printf(UT_SUPPORT);
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

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s [-d <device>] [-v]", progname);
}
