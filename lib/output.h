#include "perfdata.h"
#include "../plugins/common.h"

typedef struct {
	enum state_enum state;
	char *output;
	pd_list *perfdata;
} subcheck_t;

typedef struct subcheck_list {
	subcheck_t subcheck;
	struct subcheck_list *next;
	pd_list *perfdata;
} subcheck_list_t;

enum output_format_t {
	CLASSIC_FORMAT,
	ICINGA2_FORMAT,
	SUMMARY_ONLY
};

typedef struct {
	enum state_enum state;
	enum output_format_t format;
	char *summary;
	pd_list* perfdata;
	struct subcheck_list *subchecks;
} check_t;

extern check_t check;

void init_check();

void set_output_format(enum output_format_t);

int add_subcheck(enum state_enum, char *, pd_list *);

void add_summary(char *summary);

void cleanup_check();

void print_output();
