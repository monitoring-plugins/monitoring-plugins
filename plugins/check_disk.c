/*****************************************************************************
* 
* Nagios check_disk plugin
* 
* License: GPL
* Copyright (c) 1999-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_disk plugin
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

const char *progname = "check_disk";
const char *program_name = "check_disk";  /* Required for coreutils libs */
const char *copyright = "1999-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";


#include "common.h"
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#include <assert.h>
#include "popen.h"
#include "utils.h"
#include "utils_disk.h"
#include <stdarg.h>
#include "fsusage.h"
#include "mountlist.h"
#include "intprops.h"	/* necessary for TYPE_MAXIMUM */
#if HAVE_LIMITS_H
# include <limits.h>
#endif
#include "regex.h"


/* If nonzero, show inode information. */
static int inode_format = 1;

/* If nonzero, show even filesystems with zero size or
   uninteresting types. */
static int show_all_fs = 1;

/* If nonzero, show only local filesystems.  */
static int show_local_fs = 0;

/* If nonzero, show only local filesystems but call stat() on remote ones. */
static int stat_remote_fs = 0;

/* If positive, the units to use when printing sizes;
   if negative, the human-readable base.  */
/* static int output_block_size; */

/* If nonzero, invoke the `sync' system call before getting any usage data.
   Using this option can make df very slow, especially with many or very
   busy disks.  Note that this may make a difference on some systems --
   SunOs4.1.3, for one.  It is *not* necessary on Linux.  */
/* static int require_sync = 0; */

/* Linked list of filesystem types to display.
   If `fs_select_list' is NULL, list all types.
   This table is generated dynamically from command-line options,
   rather than hardcoding into the program what it thinks are the
   valid filesystem types; let the user specify any filesystem type
   they want to, and if there are any filesystems of that type, they
   will be shown.

   Some filesystem types:
   4.2 4.3 ufs nfs swap ignore io vm efs dbg */

/* static struct parameter_list *fs_select_list; */

/* Linked list of filesystem types to omit.
   If the list is empty, don't exclude any types.  */
static struct name_list *fs_exclude_list;

/* Linked list of filesystem types to check.
   If the list is empty, include all types.  */
static struct name_list *fs_include_list;

static struct name_list *dp_exclude_list;

static struct parameter_list *path_select_list = NULL;

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
void set_all_thresholds (struct parameter_list *path);
int validate_arguments (uintmax_t, uintmax_t, double, double, double, double, char *);
void print_help (void);
void print_usage (void);
double calculate_percent(uintmax_t, uintmax_t);
void stat_path (struct parameter_list *p);
void get_stats (struct parameter_list *p, struct fs_usage *fsp);
void get_path_stats (struct parameter_list *p, struct fs_usage *fsp);

double w_dfp = -1.0;
double c_dfp = -1.0;
char *path;
char *exclude_device;
char *units;
uintmax_t mult = 1024 * 1024;
int verbose = 0;
int erronly = FALSE;
int display_mntp = FALSE;
int exact_match = FALSE;
int freespace_ignore_reserved = FALSE;
char *warn_freespace_units = NULL;
char *crit_freespace_units = NULL;
char *warn_freespace_percent = NULL;
char *crit_freespace_percent = NULL;
char *warn_usedspace_units = NULL;
char *crit_usedspace_units = NULL;
char *warn_usedspace_percent = NULL;
char *crit_usedspace_percent = NULL;
char *warn_usedinodes_percent = NULL;
char *crit_usedinodes_percent = NULL;
char *warn_freeinodes_percent = NULL;
char *crit_freeinodes_percent = NULL;
int path_selected = FALSE;
char *group = NULL;
struct stat *stat_buf;
struct name_list *seen = NULL;


int
main (int argc, char **argv)
{
  int result = STATE_UNKNOWN;
  int disk_result = STATE_UNKNOWN;
  char *output;
  char *details;
  char *perf;
  char *preamble;
  double inode_space_pct;
  double warning_high_tide;
  double critical_high_tide;
  int temp_result;

  struct mount_entry *me;
  struct fs_usage fsp, tmpfsp;
  struct parameter_list *temp_list, *path;

  preamble = strdup (" - free space:");
  output = strdup ("");
  details = strdup ("");
  perf = strdup ("");
  stat_buf = malloc(sizeof *stat_buf);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  mount_list = read_file_system_list (0);

  /* Parse extra opts if any */
  argv = np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage4 (_("Could not parse arguments"));

  /* If a list of paths has not been selected, find entire
     mount list and create list of paths
   */
  if (path_selected == FALSE) {
    for (me = mount_list; me; me = me->me_next) {
      if (! (path = np_find_parameter(path_select_list, me->me_mountdir))) {
        path = np_add_parameter(&path_select_list, me->me_mountdir);
      }
      path->best_match = me;
      path->group = group;
      set_all_thresholds(path);
    }
  }
  np_set_best_match(path_select_list, mount_list, exact_match);

  /* Error if no match found for specified paths */
  temp_list = path_select_list;

  while (temp_list) {
    if (! temp_list->best_match) {
      die (STATE_CRITICAL, _("DISK %s: %s not found\n"), _("CRITICAL"), temp_list->name);
    }

    temp_list = temp_list->name_next;
  }

  /* Process for every path in list */
  for (path = path_select_list; path; path=path->name_next) {

    if (verbose >= 3 && path->freespace_percent->warning != NULL && path->freespace_percent->critical != NULL)
      printf("Thresholds(pct) for %s warn: %f crit %f\n",path->name, path->freespace_percent->warning->end,
                                                         path->freespace_percent->critical->end);

    if (verbose >= 3 && path->group != NULL)
      printf("Group of %s: %s\n",path->name,path->group);

    /* reset disk result */
    disk_result = STATE_UNKNOWN;

    me = path->best_match;

    /* Filters */

    /* Remove filesystems already seen */
    if (np_seen_name(seen, me->me_mountdir)) {
      continue;
    } 
    np_add_name(&seen, me->me_mountdir);

    if (path->group == NULL) {
      /* Skip remote filesystems if we're not interested in them */
      if (me->me_remote && show_local_fs) {
        if (stat_remote_fs)
          stat_path(path);
        continue;
      /* Skip pseudo fs's if we haven't asked for all fs's */
      } else if (me->me_dummy && !show_all_fs) {
        continue;
      /* Skip excluded fstypes */
      } else if (fs_exclude_list && np_find_name (fs_exclude_list, me->me_type)) {
        continue;
      /* Skip excluded fs's */
      } else if (dp_exclude_list &&
               (np_find_name (dp_exclude_list, me->me_devname) ||
                np_find_name (dp_exclude_list, me->me_mountdir))) {
        continue;
      /* Skip not included fstypes */
      } else if (fs_include_list && !np_find_name (fs_include_list, me->me_type)) {
        continue;
      }
    }

    stat_path(path);
    get_fs_usage (me->me_mountdir, me->me_devname, &fsp);

    if (fsp.fsu_blocks && strcmp ("none", me->me_mountdir)) {
      get_stats (path, &fsp);

      if (verbose >= 3) {
        printf ("For %s, used_pct=%g free_pct=%g used_units=%g free_units=%g total_units=%g used_inodes_pct=%g free_inodes_pct=%g fsp.fsu_blocksize=%llu mult=%llu\n",
          me->me_mountdir, path->dused_pct, path->dfree_pct, path->dused_units, path->dfree_units, path->dtotal_units, path->dused_inodes_percent, path->dfree_inodes_percent, fsp.fsu_blocksize, mult);
      }

      /* Threshold comparisons */

      temp_result = get_status(path->dfree_units, path->freespace_units);
      if (verbose >=3) printf("Freespace_units result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      temp_result = get_status(path->dfree_pct, path->freespace_percent);
      if (verbose >=3) printf("Freespace%% result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      temp_result = get_status(path->dused_units, path->usedspace_units);
      if (verbose >=3) printf("Usedspace_units result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      temp_result = get_status(path->dused_pct, path->usedspace_percent);
      if (verbose >=3) printf("Usedspace_percent result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      temp_result = get_status(path->dused_inodes_percent, path->usedinodes_percent);
      if (verbose >=3) printf("Usedinodes_percent result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      temp_result = get_status(path->dfree_inodes_percent, path->freeinodes_percent);
      if (verbose >=3) printf("Freeinodes_percent result=%d\n", temp_result);
      disk_result = max_state( disk_result, temp_result );

      result = max_state(result, disk_result);

      /* What a mess of units. The output shows free space, the perf data shows used space. Yikes!
         Hack here. Trying to get warn/crit levels from freespace_(units|percent) for perf
         data. Assumption that start=0. Roll on new syntax...
      */

      /* *_high_tide must be reinitialized at each run */
      warning_high_tide = UINT_MAX;
      critical_high_tide = UINT_MAX;

      if (path->freespace_units->warning != NULL) {
        warning_high_tide = path->dtotal_units - path->freespace_units->warning->end;
      }
      if (path->freespace_percent->warning != NULL) {
        warning_high_tide = abs( min( (double) warning_high_tide, (double) (1.0 - path->freespace_percent->warning->end/100)*path->dtotal_units ));
      }
      if (path->freespace_units->critical != NULL) {
        critical_high_tide = path->dtotal_units - path->freespace_units->critical->end;
      }
      if (path->freespace_percent->critical != NULL) {
        critical_high_tide = abs( min( (double) critical_high_tide, (double) (1.0 - path->freespace_percent->critical->end/100)*path->dtotal_units ));
      }

      /* Nb: *_high_tide are unset when == UINT_MAX */
      xasprintf (&perf, "%s %s", perf,
                perfdata ((!strcmp(me->me_mountdir, "none") || display_mntp) ? me->me_devname : me->me_mountdir,
                          path->dused_units, units,
                          (warning_high_tide != UINT_MAX ? TRUE : FALSE), warning_high_tide,
                          (critical_high_tide != UINT_MAX ? TRUE : FALSE), critical_high_tide,
                          TRUE, 0,
                          TRUE, path->dtotal_units));

      if (disk_result==STATE_OK && erronly && !verbose)
        continue;

      xasprintf (&output, "%s %s %.0f %s (%.0f%%",
                output,
                (!strcmp(me->me_mountdir, "none") || display_mntp) ? me->me_devname : me->me_mountdir,
                path->dfree_units,
                units,
                path->dfree_pct);
      if (path->dused_inodes_percent < 0) {
        xasprintf(&output, "%s inode=-);", output);
      } else {
        xasprintf(&output, "%s inode=%.0f%%);", output, path->dfree_inodes_percent );
      }

      /* TODO: Need to do a similar debug line
      xasprintf (&details, _("%s\n\
%.0f of %.0f %s (%.0f%% inode=%.0f%%) free on %s (type %s mounted on %s) warn:%lu crit:%lu warn%%:%.0f%% crit%%:%.0f%%"),
                details, dfree_units, dtotal_units, units, dfree_pct, inode_space_pct,
                me->me_devname, me->me_type, me->me_mountdir,
                (unsigned long)w_df, (unsigned long)c_df, w_dfp, c_dfp);
      */

    }

  }

  if (verbose >= 2)
    xasprintf (&output, "%s%s", output, details);


  printf ("DISK %s%s%s|%s\n", state_text (result), (erronly && result==STATE_OK) ? "" : preamble, output, perf);
  return result;
}


double calculate_percent(uintmax_t value, uintmax_t total) {
  double pct = -1;
  /* I don't understand the below, but it is taken from coreutils' df */
  /* Seems to be calculating pct, in the best possible way */
  if (value <= TYPE_MAXIMUM(uintmax_t) / 100
    && total != 0) {
    uintmax_t u100 = value * 100;
    pct = u100 / total + (u100 % total != 0);
  } else {
    /* Possible rounding errors - see coreutils' df for more explanation */
    double u = value;
    double t = total;
    if (t) {
      long int lipct = pct = u * 100 / t;
      double ipct = lipct;

      /* Like 'pct = ceil (dpct);', but without ceil - from coreutils again */
      if (ipct - 1 < pct && pct <= ipct + 1)
        pct = ipct + (ipct < pct);
    }
  }
  return pct;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c, err;
  struct parameter_list *se;
  struct parameter_list *temp_list = NULL, *previous = NULL;
  struct parameter_list *temp_path_select_list = NULL;
  struct mount_entry *me, *temp_me;
  int result = OK;
  regex_t re;
  int cflags = REG_NOSUB | REG_EXTENDED;
  int default_cflags = cflags;
  char errbuf[MAX_INPUT_BUFFER];
  int fnd = 0;

  int option = 0;
  static struct option longopts[] = {
    {"timeout", required_argument, 0, 't'},
    {"warning", required_argument, 0, 'w'},
    {"critical", required_argument, 0, 'c'},
    {"iwarning", required_argument, 0, 'W'},
    /* Dang, -C is taken. We might want to reshuffle this. */
    {"icritical", required_argument, 0, 'K'},
    {"kilobytes", no_argument, 0, 'k'},
    {"megabytes", no_argument, 0, 'm'},
    {"units", required_argument, 0, 'u'},
    {"path", required_argument, 0, 'p'},
    {"partition", required_argument, 0, 'p'},
    {"exclude_device", required_argument, 0, 'x'},
    {"exclude-type", required_argument, 0, 'X'},
    {"include-type", required_argument, 0, 'N'},
    {"group", required_argument, 0, 'g'},
    {"eregi-path", required_argument, 0, 'R'},
    {"eregi-partition", required_argument, 0, 'R'},
    {"ereg-path", required_argument, 0, 'r'},
    {"ereg-partition", required_argument, 0, 'r'},
    {"freespace-ignore-reserved", no_argument, 0, 'f'},
    {"ignore-ereg-path", required_argument, 0, 'i'},
    {"ignore-ereg-partition", required_argument, 0, 'i'},
    {"ignore-eregi-path", required_argument, 0, 'I'},
    {"ignore-eregi-partition", required_argument, 0, 'I'},
    {"local", no_argument, 0, 'l'},
    {"stat-remote-fs", no_argument, 0, 'L'},
    {"mountpoint", no_argument, 0, 'M'},
    {"errors-only", no_argument, 0, 'e'},
    {"exact-match", no_argument, 0, 'E'},
    {"all", no_argument, 0, 'A'},
    {"verbose", no_argument, 0, 'v'},
    {"quiet", no_argument, 0, 'q'},
    {"clear", no_argument, 0, 'C'},
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  np_add_name(&fs_exclude_list, "iso9660");

  for (c = 1; c < argc; c++)
    if (strcmp ("-to", argv[c]) == 0)
      strcpy (argv[c], "-t");

  while (1) {
    c = getopt_long (argc, argv, "+?VqhvefCt:c:w:K:W:u:p:x:X:N:mklLg:R:r:i:I:MEA", longopts, &option);

    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case 't':                 /* timeout period */
      if (is_integer (optarg)) {
        timeout_interval = atoi (optarg);
        break;
      }
      else {
        usage2 (_("Timeout interval must be a positive integer"), optarg);
      }

    /* See comments for 'c' */
    case 'w':                 /* warning threshold */
      if (strstr(optarg, "%")) {
        if (*optarg == '@') {
          warn_freespace_percent = optarg;
        } else {
          xasprintf(&warn_freespace_percent, "@%s", optarg);
        }
      } else {
        if (*optarg == '@') {
          warn_freespace_units = optarg;
        } else {
          xasprintf(&warn_freespace_units, "@%s", optarg);
        }
      }
      break;

    /* Awful mistake where the range values do not make sense. Normally,
       you alert if the value is within the range, but since we are using
       freespace, we have to alert if outside the range. Thus we artifically
       force @ at the beginning of the range, so that it is backwards compatible
    */
    case 'c':                 /* critical threshold */
      if (strstr(optarg, "%")) {
        if (*optarg == '@') {
          crit_freespace_percent = optarg;
        } else {
          xasprintf(&crit_freespace_percent, "@%s", optarg);
        }
      } else {
        if (*optarg == '@') {
          crit_freespace_units = optarg;
        } else {
          xasprintf(&crit_freespace_units, "@%s", optarg);
        }
      }
      break;

    case 'W':			/* warning inode threshold */
      if (*optarg == '@') {
        warn_freeinodes_percent = optarg;
      } else {
        xasprintf(&warn_freeinodes_percent, "@%s", optarg);
      }
      break;
    case 'K':			/* critical inode threshold */
      if (*optarg == '@') {
        crit_freeinodes_percent = optarg;
      } else {
        xasprintf(&crit_freeinodes_percent, "@%s", optarg);
      }
      break;
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
    case 'L':
      stat_remote_fs = 1;
    case 'l':
      show_local_fs = 1;
      break;
    case 'p':                 /* select path */
      if (! (warn_freespace_units || crit_freespace_units || warn_freespace_percent ||
             crit_freespace_percent || warn_usedspace_units || crit_usedspace_units ||
             warn_usedspace_percent || crit_usedspace_percent || warn_usedinodes_percent ||
             crit_usedinodes_percent || warn_freeinodes_percent || crit_freeinodes_percent )) {
        die (STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set a threshold value before using -p\n"));
      }

      /* add parameter if not found. overwrite thresholds if path has already been added  */
      if (! (se = np_find_parameter(path_select_list, optarg))) {
          se = np_add_parameter(&path_select_list, optarg);
      }
      se->group = group;
      set_all_thresholds(se);

      /* With autofs, it is required to stat() the path before re-populating the mount_list */
      stat_path(se);
      /* NB: We can't free the old mount_list "just like that": both list pointers and struct
       * pointers are copied around. One of the reason it wasn't done yet is that other parts
       * of check_disk need the same kind of cleanup so it'd better be done as a whole */
      mount_list = read_file_system_list (0);
      np_set_best_match(se, mount_list, exact_match);

      path_selected = TRUE;
      break;
    case 'x':                 /* exclude path or partition */
      np_add_name(&dp_exclude_list, optarg);
      break;
    case 'X':                 /* exclude file system type */
      np_add_name(&fs_exclude_list, optarg);
      break;
    case 'N':                 /* include file system type */
      np_add_name(&fs_include_list, optarg);
      break;
    case 'v':                 /* verbose */
      verbose++;
      break;
    case 'q':                 /* TODO: this function should eventually go away (removed 2007-09-20) */
      /* verbose--; **replaced by line below**. -q was only a broken way of implementing -e */
      erronly = TRUE;
      break;
    case 'e':
      erronly = TRUE;
      break;
    case 'E':
      if (path_selected)
        die (STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set -E before selecting paths\n"));
      exact_match = TRUE;
      break;
    case 'f':
      freespace_ignore_reserved = TRUE;
      break;
    case 'g':
      if (path_selected)
        die (STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set group value before selecting paths\n"));
      group = optarg;
      break;
    case 'I':
      cflags |= REG_ICASE;
    case 'i':
      if (!path_selected)
        die (STATE_UNKNOWN, "DISK %s: %s\n", _("UNKNOWN"), _("Paths need to be selected before using -i/-I. Use -A to select all paths explicitly"));
      err = regcomp(&re, optarg, cflags);
      if (err != 0) {
        regerror (err, &re, errbuf, MAX_INPUT_BUFFER);
        die (STATE_UNKNOWN, "DISK %s: %s - %s\n",_("UNKNOWN"), _("Could not compile regular expression"), errbuf);
      }

      temp_list = path_select_list;

      previous = NULL;
      while (temp_list) {
        if (temp_list->best_match) {
          if (np_regex_match_mount_entry(temp_list->best_match, &re)) {

              if (verbose >=3)
                printf("ignoring %s matching regex\n", temp_list->name);

              temp_list = np_del_parameter(temp_list, previous);
              /* pointer to first element needs to be updated if first item gets deleted */
              if (previous == NULL)
                path_select_list = temp_list;
          } else {
            previous = temp_list;
            temp_list = temp_list->name_next;
          }
        } else {
          previous = temp_list;
          temp_list = temp_list->name_next;
        }
      }


      cflags = default_cflags;
      break;

    case 'A':
      optarg = strdup(".*");
    case 'R':
      cflags |= REG_ICASE;
    case 'r':
      if (! (warn_freespace_units || crit_freespace_units || warn_freespace_percent ||
             crit_freespace_percent || warn_usedspace_units || crit_usedspace_units ||
             warn_usedspace_percent || crit_usedspace_percent || warn_usedinodes_percent ||
             crit_usedinodes_percent || warn_freeinodes_percent || crit_freeinodes_percent )) {
        die (STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set a threshold value before using -r/-R\n"));
      }

      err = regcomp(&re, optarg, cflags);
      if (err != 0) {
        regerror (err, &re, errbuf, MAX_INPUT_BUFFER);
        die (STATE_UNKNOWN, "DISK %s: %s - %s\n",_("UNKNOWN"), _("Could not compile regular expression"), errbuf);
      }

      for (me = mount_list; me; me = me->me_next) {
        if (np_regex_match_mount_entry(me, &re)) {
          fnd = TRUE;
          if (verbose >= 3)
            printf("%s %s matching expression %s\n", me->me_devname, me->me_mountdir, optarg);

          /* add parameter if not found. overwrite thresholds if path has already been added  */
          if (! (se = np_find_parameter(path_select_list, me->me_mountdir))) {
            se = np_add_parameter(&path_select_list, me->me_mountdir);
          }
          se->group = group;
          set_all_thresholds(se);
        }
      }

      if (!fnd)
        die (STATE_UNKNOWN, "DISK %s: %s - %s\n",_("UNKNOWN"),
            _("Regular expression did not match any path or disk"), optarg);

      fnd = FALSE;
      path_selected = TRUE;
      np_set_best_match(path_select_list, mount_list, exact_match);
      cflags = default_cflags;

      break;
    case 'M': /* display mountpoint */
      display_mntp = TRUE;
      break;
    case 'C':
       /* add all mount entries to path_select list if no partitions have been explicitly defined using -p */
       if (path_selected == FALSE) {
         struct parameter_list *path;
         for (me = mount_list; me; me = me->me_next) {
           if (! (path = np_find_parameter(path_select_list, me->me_mountdir)))
             path = np_add_parameter(&path_select_list, me->me_mountdir);
           path->best_match = me;
           path->group = group;
           set_all_thresholds(path);
         }
      }
      warn_freespace_units = NULL;
      crit_freespace_units = NULL;
      warn_usedspace_units = NULL;
      crit_usedspace_units = NULL;
      warn_freespace_percent = NULL;
      crit_freespace_percent = NULL;
      warn_usedspace_percent = NULL;
      crit_usedspace_percent = NULL;
      warn_usedinodes_percent = NULL;
      crit_usedinodes_percent = NULL;
      warn_freeinodes_percent = NULL;
      crit_freeinodes_percent = NULL;

      path_selected = FALSE;
      group = NULL;
      break;
    case 'V':                 /* version */
      print_revision (progname, NP_VERSION);
      exit (STATE_OK);
    case 'h':                 /* help */
      print_help ();
      exit (STATE_OK);
    case '?':                 /* help */
      usage (_("Unknown argument"));
    }
  }

  /* Support for "check_disk warn crit [fs]" with thresholds at used% level */
  c = optind;
  if (warn_usedspace_percent == NULL && argc > c && is_intnonneg (argv[c]))
    warn_usedspace_percent = argv[c++];

  if (crit_usedspace_percent == NULL && argc > c && is_intnonneg (argv[c]))
    crit_usedspace_percent = argv[c++];

  if (argc > c && path == NULL) {
    se = np_add_parameter(&path_select_list, strdup(argv[c++]));
    path_selected = TRUE;
    set_all_thresholds(se);
  }

  if (units == NULL) {
    units = strdup ("MB");
    mult = (uintmax_t)1024 * 1024;
  }

  return TRUE;
}



void
print_path (const char *mypath)
{
  if (mypath == NULL)
    printf ("\n");
  else
    printf (_(" for %s\n"), mypath);
}


void
set_all_thresholds (struct parameter_list *path)
{
    if (path->freespace_units != NULL) free(path->freespace_units);
    set_thresholds(&path->freespace_units, warn_freespace_units, crit_freespace_units);
    if (path->freespace_percent != NULL) free (path->freespace_percent);
    set_thresholds(&path->freespace_percent, warn_freespace_percent, crit_freespace_percent);
    if (path->usedspace_units != NULL) free (path->usedspace_units);
    set_thresholds(&path->usedspace_units, warn_usedspace_units, crit_usedspace_units);
    if (path->usedspace_percent != NULL) free (path->usedspace_percent);
    set_thresholds(&path->usedspace_percent, warn_usedspace_percent, crit_usedspace_percent);
    if (path->usedinodes_percent != NULL) free (path->usedinodes_percent);
    set_thresholds(&path->usedinodes_percent, warn_usedinodes_percent, crit_usedinodes_percent);
    if (path->freeinodes_percent != NULL) free (path->freeinodes_percent);
    set_thresholds(&path->freeinodes_percent, warn_freeinodes_percent, crit_freeinodes_percent);
}

/* TODO: Remove?

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

  return OK;
}

*/







void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf (COPYRIGHT, copyright, email);

  printf ("%s\n", _("This plugin checks the amount of used disk space on a mounted file system"));
  printf ("%s\n", _("and generates an alert if free space is less than one of the threshold values"));

  printf ("\n\n");

  print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (" %s\n", "-w, --warning=INTEGER");
  printf ("    %s\n", _("Exit with WARNING status if less than INTEGER units of disk are free"));
  printf (" %s\n", "-w, --warning=PERCENT%");
  printf ("    %s\n", _("Exit with WARNING status if less than PERCENT of disk space is free"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("    %s\n", _("Exit with CRITICAL status if less than INTEGER units of disk are free"));
  printf (" %s\n", "-c, --critical=PERCENT%");
  printf ("    %s\n", _("Exit with CRITICAL status if less than PERCENT of disk space is free"));
  printf (" %s\n", "-W, --iwarning=PERCENT%");
  printf ("    %s\n", _("Exit with WARNING status if less than PERCENT of inode space is free"));
  printf (" %s\n", "-K, --icritical=PERCENT%");
  printf ("    %s\n", _("Exit with CRITICAL status if less than PERCENT of inode space is free"));
  printf (" %s\n", "-p, --path=PATH, --partition=PARTITION");
  printf ("    %s\n", _("Path or partition (may be repeated)"));
  printf (" %s\n", "-x, --exclude_device=PATH <STRING>");
  printf ("    %s\n", _("Ignore device (only works if -p unspecified)"));
  printf (" %s\n", "-C, --clear");
  printf ("    %s\n", _("Clear thresholds"));
  printf (" %s\n", "-E, --exact-match");
  printf ("    %s\n", _("For paths or partitions specified with -p, only check for exact paths"));
  printf (" %s\n", "-e, --errors-only");
  printf ("    %s\n", _("Display only devices/mountpoints with errors"));
  printf (" %s\n", "-f, --freespace-ignore-reserved");
  printf ("    %s\n", _("Don't account root-reserved blocks into freespace in perfdata"));
  printf (" %s\n", "-g, --group=NAME");
  printf ("    %s\n", _("Group paths. Thresholds apply to (free-)space of all partitions together"));
  printf (" %s\n", "-k, --kilobytes");
  printf ("    %s\n", _("Same as '--units kB'"));
  printf (" %s\n", "-l, --local");
  printf ("    %s\n", _("Only check local filesystems"));
  printf (" %s\n", "-L, --stat-remote-fs");
  printf ("    %s\n", _("Only check local filesystems against thresholds. Yet call stat on remote filesystems"));
  printf ("    %s\n", _("to test if they are accessible (e.g. to detect Stale NFS Handles)"));
  printf (" %s\n", "-M, --mountpoint");
  printf ("    %s\n", _("Display the mountpoint instead of the partition"));
  printf (" %s\n", "-m, --megabytes");
  printf ("    %s\n", _("Same as '--units MB'"));
  printf (" %s\n", "-A, --all");
  printf ("    %s\n", _("Explicitly select all paths. This is equivalent to -R '.*'"));
  printf (" %s\n", "-R, --eregi-path=PATH, --eregi-partition=PARTITION");
  printf ("    %s\n", _("Case insensitive regular expression for path/partition (may be repeated)"));
  printf (" %s\n", "-r, --ereg-path=PATH, --ereg-partition=PARTITION");
  printf ("    %s\n", _("Regular expression for path or partition (may be repeated)"));
  printf (" %s\n", "-I, --ignore-eregi-path=PATH, --ignore-eregi-partition=PARTITION");
  printf ("    %s\n", _("Regular expression to ignore selected path/partition (case insensitive) (may be repeated)"));
  printf (" %s\n", "-i, --ignore-ereg-path=PATH, --ignore-ereg-partition=PARTITION");
  printf ("    %s\n", _("Regular expression to ignore selected path or partition (may be repeated)"));
  printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
  printf (" %s\n", "-u, --units=STRING");
  printf ("    %s\n", _("Choose bytes, kB, MB, GB, TB (default: MB)"));
  printf (UT_VERBOSE);
  printf (" %s\n", "-X, --exclude-type=TYPE");
  printf ("    %s\n", _("Ignore all filesystems of indicated type (may be repeated)"));
  printf (" %s\n", "-N, --include-type=TYPE");
  printf ("    %s\n", _("Check only filesystems of indicated type (may be repeated)"));

  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_disk -w 10% -c 5% -p /tmp -p /var -C -w 100000 -c 50000 -p /");
  printf ("    %s\n", _("Checks /tmp and /var at 10% and 5%, and / at 100MB and 50MB"));
  printf (" %s\n", "check_disk -w 100 -c 50 -C -w 1000 -c 500 -g sidDATA -r '^/oracle/SID/data.*$'");
  printf ("    %s\n", _("Checks all filesystems not matching -r at 100M and 50M. The fs matching the -r regex"));
  printf ("    %s\n", _("are grouped which means the freespace thresholds are applied to all disks together"));
  printf (" %s\n", "check_disk -w 100 -c 50 -C -w 1000 -c 500 -p /foo -C -w 5% -c 3% -p /bar");
  printf ("    %s\n", _("Checks /foo for 1000M/500M and /bar for 5/3%. All remaining volumes use 100M/50M"));

  printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s -w limit -c limit [-W limit] [-K limit] {-p path | -x device}\n", progname);
  printf ("[-C] [-E] [-e] [-f] [-g group ] [-k] [-l] [-M] [-m] [-R path ] [-r path ]\n");
  printf ("[-t timeout] [-u unit] [-v] [-X type] [-N type]\n");
}

void
stat_path (struct parameter_list *p)
{
  /* Stat entry to check that dir exists and is accessible */
  if (verbose >= 3)
    printf("calling stat on %s\n", p->name);
  if (stat (p->name, &stat_buf[0])) {
    if (verbose >= 3)
      printf("stat failed on %s\n", p->name);
    printf("DISK %s - ", _("CRITICAL"));
    die (STATE_CRITICAL, _("%s %s: %s\n"), p->name, _("is not accessible"), strerror(errno));
  }
}


void
get_stats (struct parameter_list *p, struct fs_usage *fsp) {
  struct parameter_list *p_list;
  struct fs_usage tmpfsp;
  int first = 1;

  if (p->group == NULL) {
    get_path_stats(p,fsp);
  } else {
    /* find all group members */
    for (p_list = path_select_list; p_list; p_list=p_list->name_next) {
      if (p_list->group && ! (strcmp(p_list->group, p->group))) {
        stat_path(p_list);
        get_fs_usage (p_list->best_match->me_mountdir, p_list->best_match->me_devname, &tmpfsp);
        get_path_stats(p_list, &tmpfsp); 
        if (verbose >= 3)
          printf("Group %s: adding %llu blocks sized %llu, (%s) used_units=%g free_units=%g total_units=%g fsu_blocksize=%llu mult=%llu\n",
                 p_list->group, tmpfsp.fsu_bavail, tmpfsp.fsu_blocksize, p_list->best_match->me_mountdir, p_list->dused_units, p_list->dfree_units,
                 p_list->dtotal_units, mult);

        /* prevent counting the first FS of a group twice since its parameter_list entry 
         * is used to carry the information of all file systems of the entire group */
        if (! first) {
          p->total += p_list->total;
          p->available += p_list->available;
          p->available_to_root += p_list->available_to_root;
          p->used += p_list->used;
            
          p->dused_units += p_list->dused_units;
          p->dfree_units += p_list->dfree_units;
          p->dtotal_units += p_list->dtotal_units;
          p->inodes_total += p_list->inodes_total;
          p->inodes_free  += p_list->inodes_free;
        }
        first = 0;
      }
      if (verbose >= 3) 
        printf("Group %s now has: used_units=%g free_units=%g total_units=%g fsu_blocksize=%llu mult=%llu\n",
               p->group, tmpfsp.fsu_bavail, tmpfsp.fsu_blocksize, p->best_match->me_mountdir, p->dused_units,
               p->dfree_units, p->dtotal_units, mult);
    }
    /* modify devname and mountdir for output */
    p->best_match->me_mountdir = p->best_match->me_devname = p->group;
  }
  /* finally calculate percentages for either plain FS or summed up group */
  p->dused_pct = calculate_percent( p->used, p->used + p->available );	/* used + available can never be > uintmax */
  p->dfree_pct = 100 - p->dused_pct;
  p->dused_inodes_percent = calculate_percent(p->inodes_total - p->inodes_free, p->inodes_total);
  p->dfree_inodes_percent = 100 - p->dused_inodes_percent;
  
}

void
get_path_stats (struct parameter_list *p, struct fs_usage *fsp) {
  /* 2007-12-08 - Workaround for Gnulib reporting insanely high available
  * space on BSD (the actual value should be negative but fsp->fsu_bavail
  * is unsigned) */
  p->available = fsp->fsu_bavail > fsp->fsu_bfree ? 0 : fsp->fsu_bavail;
  p->available_to_root = fsp->fsu_bfree;
  p->used = fsp->fsu_blocks - fsp->fsu_bfree;
  if (freespace_ignore_reserved) {
    /* option activated : we substract the root-reserved space from the total */
    p->total = fsp->fsu_blocks - p->available_to_root + p->available;
  } else {
    /* default behaviour : take all the blocks into account */
    p->total = fsp->fsu_blocks;
  }
  
  p->dused_units = p->used*fsp->fsu_blocksize/mult;
  p->dfree_units = p->available*fsp->fsu_blocksize/mult;
  p->dtotal_units = p->total*fsp->fsu_blocksize/mult;
  p->inodes_total = fsp->fsu_files;      /* Total file nodes. */
  p->inodes_free  = fsp->fsu_ffree;      /* Free file nodes. */
  np_add_name(&seen, p->best_match->me_mountdir);
}
