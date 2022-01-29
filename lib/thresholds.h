#include <inttypes.h>
#include <stdbool.h>

enum value_type_t {
	NONE = 0,
	INT,
	UINT,
	DOUBLE
};

typedef union {
	uint64_t pd_uint;
	int64_t pd_int;
	double pd_double;
} perfdata_value;

typedef struct range_struct {
	enum value_type_t type;
	perfdata_value start;
	bool start_infinity;		/* FALSE (default) or TRUE */
	perfdata_value end;
	bool end_infinity;
	bool alert_on;		/* OUTSIDE (default) or INSIDE */
	char* text; /* original unparsed text input */
} range;

typedef struct thresholds_struct {
	range	*warning;
	range	*critical;
} thresholds;
