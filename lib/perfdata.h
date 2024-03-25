#ifndef _PERFDATA_
#define _PERFDATA_

#include <inttypes.h>
#include <stdbool.h>

// Enum for the specific type of a perfdata_value
typedef enum pd_value_type {
	PD_TYPE_NONE = 0,
	PD_TYPE_INT,
	PD_TYPE_DOUBLE
} pd_value_type;

typedef struct {
	enum pd_value_type type;
	union {
		long long pd_int;
		double pd_double;
	};
} mp_perfdata_value;

#define OUTSIDE false
#define INSIDE  true

/*
 * New range type with generic numerical values
 */
typedef struct mp_range_struct {
	mp_perfdata_value start;
	bool start_infinity;		/* false (default) or true */

	mp_perfdata_value end;
	bool end_infinity;

	bool alert_on;		/* OUTSIDE (default) or INSIDE */
} mp_range;

/*
 * Old range type with floating point values
 */
typedef struct range_struct {
	double  start;
	bool start_infinity;
	double  end;
	int end_infinity;
	int alert_on;       /* OUTSIDE (default) or INSIDE */
	char* text; /* original unparsed text input */
} range;


/*
 * Perfdata type for storing perfdata output
 */
typedef struct perfdata_struct {
	char *label;
	char *uom;
	mp_perfdata_value value;

	bool warn_present;
	mp_range warn;

	bool crit_present;
	mp_range crit;

	bool min_present;
	mp_perfdata_value min;

	bool max_present;
	mp_perfdata_value max;
} mp_perfdata;

/*
 * List of mp_perfdata values
 */
typedef struct pd_list_struct {
	mp_perfdata data;
	struct pd_list_struct* next;
} pd_list;


/*
 * ============
 * Initializers
 * ============
*/
/*
 * Initialize mp_perfdata value. Always use this to generate a new one
 */
mp_perfdata perfdata_init();

/*
 * Initialize pd_list value. Always use this to generate a new one
 */
pd_list *pd_list_init();


/*
 * Appends a mp_perfdata value to a pd_list
 */
void pd_list_append(pd_list[1], const mp_perfdata);

#define mp_set_pd_value(P, V) \
	_Generic((V), \
			double: mp_set_pd_value_double, \
			int: mp_set_pd_value_int, \
			long: mp_set_pd_value_long, \
			long long: mp_set_pd_value_long_long \
			) (P, V)

mp_perfdata mp_set_pd_value_double(mp_perfdata, double);
mp_perfdata mp_set_pd_value_int(mp_perfdata, int);
mp_perfdata mp_set_pd_value_long(mp_perfdata, long);
mp_perfdata mp_set_pd_value_long_long(mp_perfdata, long long);


/*
 * Free the memory used by a pd_list
 */
void pd_list_free(pd_list[1]);

int cmp_perfdata_value(const mp_perfdata_value, const mp_perfdata_value);

// =================
// String formatters
// =================
/*
 * Generate string from mp_perfdata value
 */
char *pd_to_string(const mp_perfdata);

/*
 * Generate string from perfdata_value value
 */
char *pd_value_to_string(const mp_perfdata_value);

/*
 * Generate string from pd_list value for the final output
 */
char *pd_list_to_string(const pd_list);

/*
 * Generate string from a mp_range value
 */
char *mp_range_to_string(const mp_range);
char *fmt_range(const range);

#endif /* _PERFDATA_ */
