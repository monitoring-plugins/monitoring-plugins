#ifndef _PERFDATA_
#define _PERFDATA_

#include <inttypes.h>
#include <stdbool.h>

enum pd_value_type_t {
	PD_TYPE_NONE = 0,
	PD_TYPE_INT,
	PD_TYPE_DOUBLE
};

typedef struct {
	enum pd_value_type_t type;
	union {
		long long pd_int;
		double pd_double;
	};
} perfdata_value;

#define OUTSIDE false
#define INSIDE  true

typedef struct range_struct {
	perfdata_value start;
	bool start_infinity;		/* false (default) or true */
	perfdata_value end;
	bool end_infinity;
	bool alert_on;		/* OUTSIDE (default) or INSIDE */
	char* text; /* original unparsed text input */
} range;

char *range_to_string(const range);

typedef struct perfdata_struct {
	char *label;
	char *uom;
	enum pd_value_type_t type;
	bool warn_present;
	bool crit_present;
	bool min_present;
	bool max_present;
	perfdata_value value;
	range warn;
	range crit;
	perfdata_value min;
	perfdata_value max;
} perfdata;

typedef struct pd_list_struct {
	perfdata data;
	struct pd_list_struct* next;
} pd_list;

char *pd_to_string(const perfdata);
char *pd_value_to_string(const perfdata_value);

char *pd_list_to_string(const pd_list);

perfdata init_perfdata();
pd_list *init_pd_list();

void pd_list_append(pd_list[1], const perfdata);

void pd_list_free(pd_list[1]);

char *fmt_range(const range);

int cmp_perfdata_value(const perfdata_value, const perfdata_value);

#endif /* _PERFDATA_ */
