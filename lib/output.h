#ifndef _MP_OUTPUT_
#define _MP_OUTPUT_

#include "perfdata.h"
#include "../plugins/common.h"
#include "states.h"

/*
 * A partial check result
 */
typedef struct {
	mp_state_enum state; // OK, Warning, Critical ... set explicitly
	mp_state_enum default_state; // OK, Warning, Critical .. if not set explicitly
	bool state_set_explicitly; // was the state set explicitly (or should it be derived from subchecks)

	char *output; // Text output for humans ("Filesystem xyz is fine", "Could not create TCP connection to..")
	pd_list *perfdata; // Performance data for this check
	struct subcheck_list *subchecks; // subchecks deeper in the hierarchy
} mp_subcheck;

/*
 * A list of subchecks, used in subchecks and the main check
 */
typedef struct subcheck_list {
	mp_subcheck subcheck;
	struct subcheck_list *next;
} mp_subcheck_list;

/*
 * Possible output formats
 */
typedef enum output_format {
	CLASSIC_FORMAT,
	ICINGA2_FORMAT,
	SUMMARY_ONLY
} mp_output_format;

/*
 * The main state object of a plugin. Exists only ONCE per plugin.
 * This is the "root" of a tree of singular checks.
 * The final result is always derived from the children and the "worst" state
 * in the first layer of subchecks
 */
typedef struct {
	mp_output_format format; // The output format
	char *summary; // Overall summary, if not set a summary will be automatically generated
	struct subcheck_list *subchecks;
} mp_check;

mp_check mp_check_init();
mp_subcheck mp_subcheck_init();

int mp_add_subcheck_to_check(mp_check check[static 1], mp_subcheck);
int mp_add_subcheck_to_subcheck(mp_subcheck check[static 1], mp_subcheck);

void mp_add_perfdata_to_subcheck(mp_subcheck check[static 1], const mp_perfdata);

void mp_add_summary(mp_check check[static 1], char *summary);

// TODO free and stuff
//void cleanup_check(mp_check check[static 1]);

char *mp_fmt_output(mp_check);

void print_output(mp_check);

/*
 * ==================
 * Exit functionality
 * ==================
 */

void mp_exit(mp_check) __attribute__((noreturn));

#endif /* _MP_OUTPUT_ */
