#ifndef _UTILS_BASE_
#define _UTILS_BASE_
/* Header file for nagios plugins utils_base.c */

/* This file holds header information for thresholds - use this in preference to 
   individual plugin logic */

/* This has not been merged with utils.h because of problems with
   timeout_interval when other utils_*.h files use utils.h */

/* Long term, add new functions to utils_base.h for common routines
   and utils_*.h for specific to plugin routines. If routines are
   placed in utils_*.h, then these can be tested with libtap */

#define OUTSIDE 0
#define INSIDE  1

typedef struct range_struct {
	double	start;
	int	start_infinity;		/* FALSE (default) or TRUE */
	double	end;
	int	end_infinity;
	int	alert_on;		/* OUTSIDE (default) or INSIDE */
	} range;

typedef struct thresholds_struct {
	range	*warning;
	range	*critical;
	} thresholds;

range *parse_range_string (char *);
int _set_thresholds(thresholds **, char *, char *);
void set_thresholds(thresholds **, char *, char *);
int check_range(double, range *);
int get_status(double, thresholds *);

char *np_escaped_string (const char *);

void die (int, const char *, ...) __attribute__((noreturn,format(printf, 2, 3)));

/* Return codes for _set_thresholds */
#define NP_RANGE_UNPARSEABLE 1
#define NP_WARN_WITHIN_CRIT 2

#endif /* _UTILS_BASE_ */
