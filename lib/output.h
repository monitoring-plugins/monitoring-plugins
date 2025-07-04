#pragma once

#include "../config.h"
#include "./perfdata.h"
#include "./states.h"

/*
 * A partial check result
 */
typedef struct mp_subcheck mp_subcheck;
struct mp_subcheck {
	mp_state_enum state;         // OK, Warning, Critical ... set explicitly
	mp_state_enum default_state; // OK, Warning, Critical .. if not set explicitly
	bool state_set_explicitly;   // was the state set explicitly (or should it be derived from
								 // subchecks)

	char *output;      // Text output for humans ("Filesystem xyz is fine", "Could not create TCP
					   // connection to..")
	pd_list *perfdata; // Performance data for this check
	struct subcheck_list *subchecks; // subchecks deeper in the hierarchy

	// the evaluation_functions computes the state of subcheck
	mp_state_enum (*evaluation_function)(mp_subcheck);
};

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
	MP_FORMAT_MULTI_LINE,
	MP_FORMAT_TEST_JSON,
} mp_output_format;

#define MP_FORMAT_DEFAULT MP_FORMAT_MULTI_LINE

/*
 * Format related functions
 */
void mp_set_format(mp_output_format format);
mp_output_format mp_get_format(void);

// Output detail level

typedef enum output_detail_level {
	MP_DETAIL_ALL,
	MP_DETAIL_NON_OK_ONLY,
} mp_output_detail_level;

void mp_set_level_of_detail(mp_output_detail_level level);
mp_output_detail_level mp_get_level_of_detail(void);

/*
 * The main state object of a plugin. Exists only ONCE per plugin.
 * This is the "root" of a tree of singular checks.
 * The final result is always derived from the children and the "worst" state
 * in the first layer of subchecks
 */
typedef struct mp_check mp_check;
struct mp_check {
	char *summary; // Overall summary, if not set a summary will be automatically generated
	mp_subcheck_list *subchecks;

	// the evaluation_functions computes the state of check
	mp_state_enum (*evaluation_function)(mp_check);
};

mp_check mp_check_init(void);
mp_subcheck mp_subcheck_init(void);

mp_subcheck mp_set_subcheck_state(mp_subcheck, mp_state_enum);
mp_subcheck mp_set_subcheck_default_state(mp_subcheck, mp_state_enum);

int mp_add_subcheck_to_check(mp_check check[static 1], mp_subcheck);
int mp_add_subcheck_to_subcheck(mp_subcheck check[static 1], mp_subcheck);

void mp_add_perfdata_to_subcheck(mp_subcheck check[static 1], mp_perfdata);

void mp_add_summary(mp_check check[static 1], char *summary);

mp_state_enum mp_compute_check_state(mp_check);
mp_state_enum mp_compute_subcheck_state(mp_subcheck);

mp_state_enum mp_eval_ok(mp_check overall);
mp_state_enum mp_eval_warning(mp_check overall);
mp_state_enum mp_eval_critical(mp_check overall);
mp_state_enum mp_eval_unknown(mp_check overall);
mp_state_enum mp_eval_check_default(mp_check check);
mp_state_enum mp_eval_subcheck_default(mp_subcheck subcheck);

typedef struct {
	bool parsing_success;
	mp_output_format output_format;
} parsed_output_format;
parsed_output_format mp_parse_output_format(char *format_string);

// TODO free and stuff
// void mp_cleanup_check(mp_check check[static 1]);

char *mp_fmt_output(mp_check);

void mp_print_output(mp_check);

/*
 * ==================
 * Exit functionality
 * ==================
 */

void mp_exit(mp_check) __attribute__((noreturn));
