#include <inttypes.h>
#include <stdbool.h>

enum pd_type_t {
	INT,
	UINT,
	FLOAT
};

typedef union {
	uint64_t pd_uint;
	int64_t pd_int;
	float pd_float;
} pd_value;

typedef struct {
	char *label;
	char *uom;
	enum pd_type_t type;
	bool warn_present;
	bool crit_present;
	bool min_present;
	bool max_present;
	pd_value value;
	pd_value warn;
	pd_value crit;
	pd_value min;
	pd_value max;
} perfdata_t ;

typedef struct pd_list_t pd_list;

struct pd_list_t {
	perfdata_t data;
	pd_list* next;
};

char *pd_to_string(perfdata_t);
char *pd_list_to_string(pd_list *);
