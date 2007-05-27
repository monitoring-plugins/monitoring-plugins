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
void print_thresholds(const char *, thresholds *);
int check_range(double, range *);
int get_status(double, thresholds *);

char *np_escaped_string (const char *);

void die (int, const char *, ...) __attribute__((noreturn,format(printf, 2, 3)));

/* Return codes for _set_thresholds */
#define NP_RANGE_UNPARSEABLE 1
#define NP_WARN_WITHIN_CRIT 2

/* a simple check to see if we're running as root.  
 * returns zero on failure, nonzero on success */
int np_check_if_root(void);
/* and a helpful wrapper around that.  it returns the same status
 * code from the above function, in case it's helpful for testing */
int np_warn_if_not_root(void);

#endif /* _UTILS_BASE_ */
