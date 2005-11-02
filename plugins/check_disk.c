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
 
*****************************************************************************/

const char *progname = "check_disk";
const char *program_name = "check_disk";	/* Required for coreutils libs */
const char *revision = "$Revision$";
const char *copyright = "1999-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

 /*
  * Additional inode code by Jorgen Lundman <lundman@lundman.net>
  */


#include "common.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#include <assert.h>
#include "popen.h"
#include "utils.h"
#include <stdarg.h>
#include "../lib/fsusage.h"
#include "../lib/mountlist.h"
#if HAVE_LIMITS_H
# include <limits.h>
#endif

/* If nonzero, show inode information. */
static int inode_format;

/* If nonzero, show even filesystems with zero size or
   uninteresting types. */
static int show_all_fs = 1;

/* If nonzero, show only local filesystems.  */
static int show_local_fs = 0;

/* If positive, the units to use when printing sizes;
   if negative, the human-readable base.  */
/* static int output_block_size; */

/* If nonzero, invoke the `sync' system call before getting any usage data.
   Using this option can make df very slow, especially with many or very
   busy disks.  Note that this may make a difference on some systems --
   SunOs4.1.3, for one.  It is *not* necessary on Linux.  */
/* static int require_sync = 0; */

/* A filesystem type to display. */

struct name_list
{
  char *name;
  int found;
  int found_len;
  uintmax_t w_df;
  uintmax_t c_df;
  double w_dfp;
  double c_dfp;
  double w_idfp;
  double c_idfp;
  struct name_list *name_next;
};

/* Linked list of filesystem types to display.
   If `fs_select_list' is NULL, list all types.
   This table is generated dynamically from command-line options,
   rather than hardcoding into the program what it thinks are the
   valid filesystem types; let the user specify any filesystem type
   they want to, and if there are any filesystems of that type, they
   will be shown.

   Some filesystem types:
   4.2 4.3 ufs nfs swap ignore io vm efs dbg */

/* static struct name_list *fs_select_list; */

/* Linked list of filesystem types to omit.
   If the list is empty, don't exclude any types.  */

static struct name_list *fs_exclude_list;

static struct name_list *dp_exclude_list;

static struct name_list *path_select_list;

static struct name_list *dev_select_list;

/* Linked list of mounted filesystems. */
static struct mount_entry *mount_list;

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  SYNC_OPTION = CHAR_MAX + 1,
  NO_SYNC_OPTION,
  BLOCK_SIZE_OPTION
};

#ifdef _AIX
 #pragma alloca
#endif

int process_arguments (int, char **);
void print_path (const char *mypath);
int validate_arguments (uintmax_t, uintmax_t, double, double, double, double, char *);
int check_disk (double usp, uintmax_t free_disk, double uisp);
int walk_name_list (struct name_list *list, const char *name);
void print_help (void);
void print_usage (void);

uintmax_t w_df = 0;
uintmax_t c_df = 0;
double w_dfp = -1.0;
double c_dfp = -1.0;
double w_idfp = -1.0;
double c_idfp = -1.0;
char *path;
char *exclude_device;
char *units;
uintmax_t mult = 1024 * 1024;
int verbose = 0;
int erronly = FALSE;
int display_mntp = FALSE;

/* Linked list of mounted filesystems. */
static struct mount_entry *mount_list;



int
main (int argc, char **argv)
{
	double usp = -1.0, uisp = -1.0;
	int result = STATE_UNKNOWN;
	int disk_result = STATE_UNKNOWN;
	char file_system[MAX_INPUT_BUFFER];
	char *output;
	char *details;
	char *perf;
	uintmax_t psize;
	float free_space, free_space_pct, total_space, inode_space_pct;

	struct mount_entry *me;
	struct fs_usage fsp;
	struct name_list *temp_list;

	output = strdup (" - free space:");
	details = strdup ("");
	perf = strdup ("");

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	mount_list = read_filesystem_list (0);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* if a list of paths has been selected, preseed the list with
	 * the longest matching filesystem name by iterating across
	 * the mountlist once ahead of time.  this will allow a query on
	 * "/var/log" to return information about "/var" if no "/var/log"
	 * filesystem exists, etc.  this is the default behavior already
	 * with df-based checks, but for systems with their own space
	 * checking routines, this should make them more consistent.
	 */
	if(path_select_list){
		for (me = mount_list; me; me = me->me_next) {
			walk_name_list(path_select_list, me->me_mountdir);
			walk_name_list(path_select_list, me->me_devname);
		}
		/* now pretend we never saw anything, but keep found_len.
		 * thus future searches will only match the best match */
		for (temp_list = path_select_list; temp_list; temp_list=temp_list->name_next){
			temp_list->found=0;
		}
	}

	/* for every mount entry */
	for (me = mount_list; me; me = me->me_next) {
		/* if there's a list of paths to select, the current mount
		 * entry matches in path or device name, get fs usage */
		if (path_select_list &&
		     (walk_name_list (path_select_list, me->me_mountdir) ||
		      walk_name_list (path_select_list, me->me_devname) ) ) {
			get_fs_usage (me->me_mountdir, me->me_devname, &fsp);
		/* else if there's a list of paths/devices to select (but
		 * we didn't match above) skip to the next mount entry */
		} else if (dev_select_list || path_select_list) {
			continue;
		/* skip remote filesystems if we're not interested in them */
		} else if (me->me_remote && show_local_fs) {
			continue;
		/* skip pseudo fs's if we haven't asked for all fs's */
		} else if (me->me_dummy && !show_all_fs) {
			continue;
		/* skip excluded fstypes */
		} else if (fs_exclude_list && walk_name_list (fs_exclude_list, me->me_type)) {
			continue;
		/* skip excluded fs's */	
		} else if (dp_exclude_list && 
		         (walk_name_list (dp_exclude_list, me->me_devname) ||
		          walk_name_list (dp_exclude_list, me->me_mountdir))) {
			continue;
		/* otherwise, get fs usage */
		} else {
			get_fs_usage (me->me_mountdir, me->me_devname, &fsp);
		}

		if (fsp.fsu_blocks && strcmp ("none", me->me_mountdir)) {
			usp = (double)(fsp.fsu_blocks - fsp.fsu_bavail) * 100 / fsp.fsu_blocks;
                        uisp = (double)(fsp.fsu_files - fsp.fsu_ffree) * 100 / fsp.fsu_files;
			disk_result = check_disk (usp, fsp.fsu_bavail, uisp);


			result = max_state (disk_result, result);
			psize = fsp.fsu_blocks*fsp.fsu_blocksize/mult;


                        /* Moved this computation up here so we can add it
                         * to perf */
                        inode_space_pct = (float)fsp.fsu_ffree*100/fsp.fsu_files;


			asprintf (&perf, "%s %s", perf,
			          perfdata ((!strcmp(file_system, "none") || display_mntp) ? me->me_devname : me->me_mountdir,
			                    psize-(fsp.fsu_bavail*fsp.fsu_blocksize/mult), units,
			                    TRUE, min ((uintmax_t)psize-(uintmax_t)w_df, (uintmax_t)((1.0-w_dfp/100.0)*psize)),
			                    TRUE, min ((uintmax_t)psize-(uintmax_t)c_df, (uintmax_t)((1.0-c_dfp/100.0)*psize)),
                                            TRUE, inode_space_pct,

			                    TRUE, psize));
			if (disk_result==STATE_OK && erronly && !verbose)
				continue;

			free_space = (float)fsp.fsu_bavail*fsp.fsu_blocksize/mult;
			free_space_pct = (float)fsp.fsu_bavail*100/fsp.fsu_blocks;
			total_space = (float)fsp.fsu_blocks*fsp.fsu_blocksize/mult;
			if (disk_result!=STATE_OK || verbose>=0)
				asprintf (&output, ("%s %s %.0f %s (%.0f%% inode=%.0f%%);"),
				          output,
				          (!strcmp(file_system, "none") || display_mntp) ? me->me_devname : me->me_mountdir,
				          free_space,
				          units,
					  free_space_pct,
					  inode_space_pct);

			asprintf (&details, _("%s\n\
%.0f of %.0f %s (%.0f%% inode=%.0f%%) free on %s (type %s mounted on %s) warn:%lu crit:%lu warn%%:%.0f%% crit%%:%.0f%%"),
			          details, free_space, total_space, units, free_space_pct, inode_space_pct,
			          me->me_devname, me->me_type, me->me_mountdir,
			          (unsigned long)w_df, (unsigned long)c_df, w_dfp, c_dfp);

		}

	}

	asprintf (&output, "%s|%s", output, perf);

	if (verbose > 2)
		asprintf (&output, "%s%s", output, details);

	/* Override result if paths specified and not found */
	temp_list = path_select_list;
	while (temp_list) {
		if (!temp_list->found) {
			asprintf (&output, _("%s [%s not found]"), output, temp_list->name);
			result = STATE_CRITICAL;
		}
		temp_list = temp_list->name_next;
	}

	printf ("DISK %s%s\n", state_text (result), output);
	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	struct name_list *se;
	struct name_list **pathtail = &path_select_list;
	struct name_list **fstail = &fs_exclude_list;
	struct name_list **dptail = &dp_exclude_list;
	struct name_list *temp_list;
	int result = OK;

	unsigned long l;

	int option = 0;
	static struct option longopts[] = {
		{"timeout", required_argument, 0, 't'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"iwarning", required_argument, 0, 'W'},
		/* Dang, -C is taken. We might want to reshuffle this. */
		{"icritical", required_argument, 0, 'K'},
		{"local", required_argument, 0, 'l'},
		{"kilobytes", required_argument, 0, 'k'},
		{"megabytes", required_argument, 0, 'm'},
		{"units", required_argument, 0, 'u'},
		{"path", required_argument, 0, 'p'},
		{"partition", required_argument, 0, 'p'},
		{"exclude_device", required_argument, 0, 'x'},
		{"exclude-type", required_argument, 0, 'X'},
		{"mountpoint", no_argument, 0, 'M'},
		{"errors-only", no_argument, 0, 'e'},
		{"verbose", no_argument, 0, 'v'},
		{"quiet", no_argument, 0, 'q'},
		{"clear", no_argument, 0, 'C'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	se = (struct name_list *) malloc (sizeof (struct name_list));
	se->name = strdup ("iso9660");
	se->name_next = NULL;
	se->found = 0;
	se->found_len = 0;
	*fstail = se;
	fstail = &se->name_next;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+?VqhveCt:c:w:K:W:u:p:x:X:mklM", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 't':									/* timeout period */
			if (is_integer (optarg)) {
				timeout_interval = atoi (optarg);
				break;
			}
			else {
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			}
		case 'w':									/* warning threshold */
			if (is_intnonneg (optarg)) {
				w_df = atoi (optarg);
				break;
			}
			else if (strpbrk (optarg, ",:") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%lu%*[:,]%lf%%", &l, &w_dfp) == 2) {
				w_df = (uintmax_t)l;
				break;
			}
			else if (strstr (optarg, "%") && sscanf (optarg, "%lf%%", &w_dfp) == 1) {
				break;
			}
			else {
				usage4 (_("Warning threshold must be integer or percentage!"));
			}
		case 'c':									/* critical threshold */
			if (is_intnonneg (optarg)) {
				c_df = atoi (optarg);
				break;
			}
			else if (strpbrk (optarg, ",:") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%lu%*[,:]%lf%%", &l, &c_dfp) == 2) {
				c_df = (uintmax_t)l;
				break;
			}
			else if (strstr (optarg, "%") && sscanf (optarg, "%lf%%", &c_dfp) == 1) {
				break;
			}
			else {
				usage4 (_("Critical threshold must be integer or percentage!"));
			}


                case 'W':                                                                       /* warning inode threshold */
                        if (strstr (optarg, "%") && sscanf (optarg, "%lf%%", &w_idfp) == 1) {
                        break;
                        }
                        else {
                   		usage (_("Warning inode threshold must be percentage!\n"));
                 	}
                case 'K':                                                                       /* kritical inode threshold */
                        if (strstr (optarg, "%") && sscanf (optarg, "%lf%%", &c_idfp) == 1) {
                        break;
                        }
                        else {
                   		usage (_("Critical inode threshold must be percentage!\n"));
                       }
		case 'u':
			if (units)
				free(units);
			if (! strcmp (optarg, "bytes")) {
				mult = (uintmax_t)1;
				units = strdup ("B");
			} else if (! strcmp (optarg, "kB")) {
				mult = (uintmax_t)1024;
				units = strdup ("kB");
			} else if (! strcmp (optarg, "MB")) {
				mult = (uintmax_t)1024 * 1024;
				units = strdup ("MB");
			} else if (! strcmp (optarg, "GB")) {
				mult = (uintmax_t)1024 * 1024 * 1024;
				units = strdup ("GB");
			} else if (! strcmp (optarg, "TB")) {
				mult = (uintmax_t)1024 * 1024 * 1024 * 1024;
				units = strdup ("TB");
			} else {
				die (STATE_UNKNOWN, _("unit type %s not known\n"), optarg);
			}
			if (units == NULL)
				die (STATE_UNKNOWN, _("failed allocating storage for '%s'\n"), "units");
			break;
		case 'k': /* display mountpoint */
			mult = 1024;
			if (units)
				free(units);
			units = strdup ("kB");
			break;
		case 'm': /* display mountpoint */
			mult = 1024 * 1024;
			if (units)
				free(units);
			units = strdup ("MB");
			break;
		case 'l':
			show_local_fs = 1;			
			break;
		case 'p':									/* select path */
			se = (struct name_list *) malloc (sizeof (struct name_list));
			se->name = optarg;
			se->name_next = NULL;
			se->w_df = w_df;
			se->c_df = c_df;
			se->w_dfp = w_dfp;
			se->c_dfp = c_dfp;
			se->found = 0;
			se->found_len = 0;
			*pathtail = se;
			pathtail = &se->name_next;
			break;
 		case 'x':									/* exclude path or partition */
			se = (struct name_list *) malloc (sizeof (struct name_list));
			se->name = optarg;
			se->name_next = NULL;

                        /* If you don't clear the w_fd etc values here, they
                         * get processed when you walk the list and assigned
                         * to the global w_df!
                         */
                        se->w_df = 0;
                        se->c_df = 0;
                        se->w_dfp = 0;
                        se->c_dfp = 0;
			se->found = 0;
			se->found_len = 0;
			*dptail = se;
			dptail = &se->name_next;
			break;
		case 'X':									/* exclude file system type */
			se = (struct name_list *) malloc (sizeof (struct name_list));
			se->name = optarg;
			se->name_next = NULL;
                        /* If you don't clear the w_fd etc values here, they
                         * get processed when you walk the list and assigned
                         * to the global w_df!
                         */
                        se->w_df = 0;
                        se->c_df = 0;
                        se->w_dfp = 0;
                        se->c_dfp = 0;
			se->found = 0;
			se->found_len = 0;
			*fstail = se;
			fstail = &se->name_next;
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 'q':									/* verbose */
			verbose--;
			break;
		case 'e':
			erronly = TRUE;
			break;
		case 'M': /* display mountpoint */
			display_mntp = TRUE;
			break;
		case 'C':
			w_df = 0;
			c_df = 0;
			w_dfp = -1.0;
			c_dfp = -1.0;
			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage2 (_("Unknown argument"), optarg);
		}
	}

	/* Support for "check_disk warn crit [fs]" with thresholds at used level */
	c = optind;
	if (w_dfp < 0 && argc > c && is_intnonneg (argv[c]))
		w_dfp = (100.0 - atof (argv[c++]));

	if (c_dfp < 0 && argc > c && is_intnonneg (argv[c]))
		c_dfp = (100.0 - atof (argv[c++]));

	if (argc > c && path == NULL) {
		se = (struct name_list *) malloc (sizeof (struct name_list));
		se->name = strdup (argv[c++]);
		se->name_next = NULL;
		se->w_df = w_df;
		se->c_df = c_df;
		se->w_dfp = w_dfp;
		se->c_dfp = c_dfp;
		se->found =0;
		se->found_len = 0;
		*pathtail = se;
	}

	if (path_select_list) {
		temp_list = path_select_list;
		while (temp_list) {
			if (validate_arguments (temp_list->w_df,
				                      temp_list->c_df,
				                      temp_list->w_dfp,
				                      temp_list->c_dfp,
				                      temp_list->w_idfp,
				                      temp_list->c_idfp,
				                      temp_list->name) == ERROR)
				result = ERROR;
			temp_list = temp_list->name_next;
		}
		return result;
	} else {
		return validate_arguments (w_df, c_df, w_dfp, c_dfp, w_idfp, c_idfp, NULL);
	}
}



void
print_path (const char *mypath) 
{
	if (mypath == NULL)
		printf ("\n");
	else
		printf (_(" for %s\n"), mypath);

	return;
}



int
validate_arguments (uintmax_t w, uintmax_t c, double wp, double cp, double iwp, double icp, char *mypath)
{
	if (w < 0 && c < 0 && wp < 0.0 && cp < 0.0) {
		printf (_("INPUT ERROR: No thresholds specified"));
		print_path (mypath);
		return ERROR;
	}
	else if ((wp >= 0.0 || cp >= 0.0) &&
	         (wp < 0.0 || cp < 0.0 || wp > 100.0 || cp > 100.0 || cp > wp)) {
		printf (_("\
INPUT ERROR: C_DFP (%f) should be less than W_DFP (%.1f) and both should be between zero and 100 percent, inclusive"),
		        cp, wp);
		print_path (mypath);
		return ERROR;
	}
	else if ((iwp >= 0.0 || icp >= 0.0) &&
	         (iwp < 0.0 || icp < 0.0 || iwp > 100.0 || icp > 100.0 || icp > iwp)) {
		printf (_("\
INPUT ERROR: C_IDFP (%f) should be less than W_IDFP (%.1f) and both should be between zero and 100 percent, inclusive"),
		        icp, iwp);
		print_path (mypath);
		return ERROR;
	}
	else if ((w > 0 || c > 0) && (w == 0 || c == 0 || c > w)) {
		printf (_("\
INPUT ERROR: C_DF (%lu) should be less than W_DF (%lu) and both should be greater than zero"),
		        (unsigned long)c, (unsigned long)w);
		print_path (mypath);
		return ERROR;
	}
	
	if (units == NULL) {
		units = strdup ("MB");
		mult = (uintmax_t)1024 * 1024;
	}
	return OK;
}



int

check_disk (double usp, uintmax_t free_disk, double uisp)
{
       int result = STATE_UNKNOWN;
       /* check the percent used space against thresholds */
       if (usp >= 0.0 && c_dfp >=0.0 && usp >= (100.0 - c_dfp))
               result = STATE_CRITICAL;
       else if (uisp >= 0.0 && c_idfp >=0.0 && uisp >= (100.0 - c_idfp))
               result = STATE_CRITICAL;
       else if (c_df > 0 && free_disk <= c_df)
               result = STATE_CRITICAL;
       else if (usp >= 0.0 && w_dfp >=0.0 && usp >= (100.0 - w_dfp))
               result = STATE_WARNING;
       else if (uisp >= 0.0 && w_idfp >=0.0 && uisp >= (100.0 - w_idfp))
               result = STATE_WARNING;
       else if (w_df > 0 && free_disk <= w_df)
               result = STATE_WARNING;
       else if (usp >= 0.0)
		result = STATE_OK;
	return result;
}



int
walk_name_list (struct name_list *list, const char *name)
{
	int name_len;
	name_len = strlen(name);
	while (list) {
		/* if the paths match up to the length of the mount path,
		 * AND if the mount path name is longer than the longest
		 * found match, we have a new winner */
		if (name_len >= list->found_len && 
		    ! strncmp(list->name, name, name_len)) {
			list->found = 1;
			list->found_len = name_len;
			/* if required for name_lists that have not saved w_df, etc (eg exclude lists) */
			if (list->w_df) w_df = list->w_df;
			if (list->c_df) c_df = list->c_df;
			if (list->w_dfp>=0.0) w_dfp = list->w_dfp;
			if (list->c_dfp>=0.0) c_dfp = list->c_dfp;
			return TRUE;
		}
		list = list->name_next;
	}
	return FALSE;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\
This plugin checks the amount of used disk space on a mounted file system\n\
and generates an alert if free space is less than one of the threshold values.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
 -w, --warning=INTEGER\n\
   Exit with WARNING status if less than INTEGER --units of disk are free\n\
 -w, --warning=PERCENT%%\n\
   Exit with WARNING status if less than PERCENT of disk space is free\n\
 -W, --iwarning=PERCENT%%\n\
   Exit with WARNING status if less than PERCENT of inode space is free\n\
 -K, --icritical=PERCENT%%\n\
   Exit with CRITICAL status if less than PERCENT of inode space is free\n\
 -c, --critical=INTEGER\n\
   Exit with CRITICAL status if less than INTEGER --units of disk are free\n\
 -c, --critical=PERCENT%%\n\
   Exit with CRITCAL status if less than PERCENT of disk space is free\n\
 -C, --clear\n\
    Clear thresholds\n"));

	printf (_("\
 -u, --units=STRING\n\
    Choose bytes, kB, MB, GB, TB (default: MB)\n\
 -k, --kilobytes\n\
    Same as '--units kB'\n\
 -m, --megabytes\n\
    Same as '--units MB'\n"));

	printf (_("\
 -l, --local\n\
    Only check local filesystems\n\
 -p, --path=PATH, --partition=PARTITION\n\
    Path or partition (may be repeated)\n\
 -x, --exclude_device=PATH <STRING>\n\
    Ignore device (only works if -p unspecified)\n\
 -X, --exclude-type=TYPE <STRING>\n\
    Ignore all filesystems of indicated type (may be repeated)\n\
 -M, --mountpoint\n\
    Display the mountpoint instead of the partition\n\
 -e, --errors-only\n\
    Display only devices/mountpoints with errors\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf ("%s", _("Examples:\n\
 check_disk -w 10% -c 5% -p /tmp -p /var -C -w 100000 -c 50000 -p /\n\
   Checks /tmp and /var at 10%,5% and / at 100MB, 50MB\n"));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s -w limit -c limit [-p path | -x device] [-t timeout] [-m] [-e] [-W limit] [-K limit]\n\
                  [-v] [-q]\n", progname);
}
