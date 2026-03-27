#pragma once

#include "../config.h"

#include <inttypes.h>
#include <stdbool.h>

// Enum for the specific type of a perfdata_value
typedef enum pd_value_type {
	PD_TYPE_NONE = 0,
	PD_TYPE_INT,
	PD_TYPE_UINT,
	PD_TYPE_DOUBLE
} pd_value_type;

typedef struct {
	enum pd_value_type type;
	union {
		long long pd_int;
		unsigned long long pd_uint;
		double pd_double;
	};
} mp_perfdata_value;

#define MP_OUTSIDE false
#define MP_INSIDE  true

/*
 * New range type with generic numerical values
 */
typedef struct {
	mp_perfdata_value start;
	bool start_infinity; /* false (default) or true */

	mp_perfdata_value end;
	bool end_infinity;

	bool alert_on_inside_range; /* OUTSIDE (default) or INSIDE */
} mp_range;

/*
 * Old range type with floating point values
 */
typedef struct {
	double start;
	bool start_infinity;
	double end;
	bool end_infinity;
	int alert_on; /* OUTSIDE (default) or INSIDE */
	char *text;   /* original unparsed text input */
} range;

/*
 * Perfdata type for storing perfdata output
 */
typedef struct {
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
	struct pd_list_struct *next;
} pd_list;

/*
 * ============
 * Initializers
 * ============
 */
/*
 * Initialize mp_perfdata value. Always use this to generate a new one
 */
mp_perfdata perfdata_init(void);

/*
 * Initialize pd_list value. Always use this to generate a new one
 */
pd_list *pd_list_init(void);

/*
 * Initialize a new mp_range value, with unset values (start and end are infinite
 */
mp_range mp_range_init(void);

/*
 * Worker functions
 */

mp_range mp_range_set_start(mp_range, mp_perfdata_value);
mp_range mp_range_set_end(mp_range, mp_perfdata_value);

/*
 * Parsing a range from a string
 */

typedef enum {
	MP_PARSING_SUCCESS = 0,
	MP_PARSING_SUCCES = MP_PARSING_SUCCESS,
	MP_PARSING_FAILURE,
	MP_RANGE_PARSING_FAILURE,
	MP_RANGE_PARSING_UNDERFLOW,
	MP_RANGE_PARSING_OVERFLOW,
	MP_RANGE_PARSING_INVALID_CHAR,
} mp_range_parser_error;

typedef struct mp_range_parsed {
	mp_range_parser_error error;
	mp_range range;
} mp_range_parsed;

mp_range_parsed mp_parse_range_string(const char * /*input*/);

/*
 * Appends a mp_perfdata value to a pd_list
 */
void pd_list_append(pd_list[1], mp_perfdata);

#define mp_set_pd_value(P, V)                                                                      \
	_Generic((V),                                                                                  \
		float: mp_set_pd_value_float,                                                              \
		double: mp_set_pd_value_double,                                                            \
		int: mp_set_pd_value_int,                                                                  \
		unsigned int: mp_set_pd_value_u_int,                                                       \
		long: mp_set_pd_value_long,                                                                \
		unsigned long: mp_set_pd_value_u_long,                                                     \
		long long: mp_set_pd_value_long_long,                                                      \
		unsigned long long: mp_set_pd_value_u_long_long)(P, V)

mp_perfdata mp_set_pd_value_float(mp_perfdata, float);
mp_perfdata mp_set_pd_value_double(mp_perfdata, double);
mp_perfdata mp_set_pd_value_int(mp_perfdata, int);
mp_perfdata mp_set_pd_value_u_int(mp_perfdata, unsigned int);
mp_perfdata mp_set_pd_value_long(mp_perfdata, long);
mp_perfdata mp_set_pd_value_u_long(mp_perfdata, unsigned long);
mp_perfdata mp_set_pd_value_long_long(mp_perfdata, long long);
mp_perfdata mp_set_pd_value_u_long_long(mp_perfdata, unsigned long long);

#define mp_create_pd_value(V)                                                                      \
	_Generic((V),                                                                                  \
		float: mp_create_pd_value_float,                                                           \
		double: mp_create_pd_value_double,                                                         \
		char: mp_create_pd_value_char,                                                             \
		unsigned char: mp_create_pd_value_u_char,                                                  \
		int: mp_create_pd_value_int,                                                               \
		unsigned int: mp_create_pd_value_u_int,                                                    \
		long: mp_create_pd_value_long,                                                             \
		unsigned long: mp_create_pd_value_u_long,                                                  \
		long long: mp_create_pd_value_long_long,                                                   \
		unsigned long long: mp_create_pd_value_u_long_long)(V)

mp_perfdata_value mp_create_pd_value_float(float);
mp_perfdata_value mp_create_pd_value_double(double);
mp_perfdata_value mp_create_pd_value_char(char);
mp_perfdata_value mp_create_pd_value_u_char(unsigned char);
mp_perfdata_value mp_create_pd_value_int(int);
mp_perfdata_value mp_create_pd_value_u_int(unsigned int);
mp_perfdata_value mp_create_pd_value_long(long);
mp_perfdata_value mp_create_pd_value_u_long(unsigned long);
mp_perfdata_value mp_create_pd_value_long_long(long long);
mp_perfdata_value mp_create_pd_value_u_long_long(unsigned long long);

mp_perfdata mp_set_pd_max_value(mp_perfdata perfdata, mp_perfdata_value value);
mp_perfdata mp_set_pd_min_value(mp_perfdata perfdata, mp_perfdata_value value);

double mp_get_pd_value(mp_perfdata_value value);

/*
 * Free the memory used by a pd_list
 */
void pd_list_free(pd_list[1]);

int cmp_perfdata_value(mp_perfdata_value, mp_perfdata_value);

// ================
// Helper functions
// ================

mp_perfdata_value mp_pd_value_multiply(mp_perfdata_value left, mp_perfdata_value right);
mp_range mp_range_multiply(mp_range range, mp_perfdata_value factor);

// =================
// String formatters
// =================
/*
 * Generate string from mp_perfdata value
 */
char *pd_to_string(mp_perfdata);

/*
 * Generate string from perfdata_value value
 */
char *pd_value_to_string(mp_perfdata_value);

/*
 * Generate string from pd_list value for the final output
 */
char *pd_list_to_string(pd_list);

/*
 * Generate string from a mp_range value
 */
char *mp_range_to_string(mp_range);
char *fmt_range(range);
