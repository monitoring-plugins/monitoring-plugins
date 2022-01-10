
#include "perfdata.h"
#include "../plugins/common.h"

typedef struct {
	enum state_enum state;
	char *output;
	pd_list perfdata;
} subcheck_t;

typedef struct subcheck_list {
	subcheck_t subcheck;
	struct subcheck_list *next;
} subcheck_list_t;

typedef struct {
	enum state_enum state;
	char *summary;
	pd_list perfdata;
	struct subcheck_list *subchecks;
} check_t;

enum output_format_t {
	CLASSIC_FORMAT,
	ICINGA2_FORMAT
};

int add_subcheck(check_t *check_object,enum state_enum state, char *output, pd_list perfdata_list);

int add_summary(check_t *check_object, char *summary);

void cleanup_check(check_t *check_object);

int print_output(check_t *, enum output_format_t);

char *print_pd_list(pd_list *);
