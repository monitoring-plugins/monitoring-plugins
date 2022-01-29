#ifndef _PERFDATA_
#define _PERFDATA_

#include <inttypes.h>
#include <stdbool.h>
#include "./thresholds.h"

typedef struct perfdata_struct {
	char *label;
	char *uom;
	enum value_type_t type;
	bool warn_present;
	bool crit_present;
	bool min_present;
	bool max_present;
	perfdata_value value;
	range warn;
	range crit;
	perfdata_value min;
	perfdata_value max;
} perfdata_t ;

typedef struct pd_list_t pd_list;

struct pd_list_t {
	perfdata_t data;
	pd_list* next;
};

char *pd_to_string(perfdata_t);

char *pd_list_to_string(pd_list *);

pd_list *new_pd_list();

void pd_list_append(pd_list *, const perfdata_t);

void pd_list_free(pd_list *);

int cmp_perfdata_value(perfdata_value *, perfdata_value *, enum value_type_t);

#endif /* _PERFDATA_ */
