#ifndef _MP_OUTPUT_
#define _MP_OUTPUT_

#include "perfdata.h"
#include "../plugins/common.h"

typedef struct {
	mp_state_enum state;
	char *output;
	pd_list *perfdata;
} mp_subcheck;

typedef struct subcheck_list {
	mp_subcheck subcheck;
	struct subcheck_list *next;
	pd_list *perfdata;
} mp_subcheck_list;

typedef enum output_format {
	CLASSIC_FORMAT,
	ICINGA2_FORMAT,
	SUMMARY_ONLY
} mp_output_format;

typedef struct {
	mp_state_enum state;
	mp_output_format format;
	char *summary;
	pd_list* perfdata;
	struct subcheck_list *subchecks;
} mp_check;

mp_check mp_check_init();
mp_subcheck mp_subcheck_init();

void set_output_format(mp_output_format);

int mp_add_subcheck(mp_check check[static 1], mp_subcheck);

void mp_add_summary(mp_check check[static 1], char *summary);

// TODO free and stuff
//void cleanup_check(mp_check *);

char *mp_fmt_output(mp_check);

static void print_output(mp_check);

#endif /* _MP_OUTPUT_ */
