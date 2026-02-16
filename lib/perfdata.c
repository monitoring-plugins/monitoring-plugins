#include "./perfdata.h"
#include "../plugins/common.h"
#include "../plugins/utils.h"
#include "utils_base.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

char *pd_value_to_string(const mp_perfdata_value pd) {
	char *result = NULL;

	assert(pd.type != PD_TYPE_NONE);

	switch (pd.type) {
	case PD_TYPE_INT:
		asprintf(&result, "%lli", pd.pd_int);
		break;
	case PD_TYPE_UINT:
		asprintf(&result, "%llu", pd.pd_int);
		break;
	case PD_TYPE_DOUBLE:
		asprintf(&result, "%f", pd.pd_double);
		break;
	default:
		// die here
		die(STATE_UNKNOWN, "Invalid mp_perfdata mode\n");
	}

	return result;
}

char *pd_to_string(mp_perfdata pd) {
	assert(pd.label != NULL);
	char *result = NULL;

	if (strchr(pd.label, '\'') == NULL) {
		asprintf(&result, "'%s'=", pd.label);
	} else {
		// we have an illegal single quote in the string
		// replace it silently instead of complaining
		for (char *ptr = pd.label; *ptr == '\0'; ptr++) {
			if (*ptr == '\'') {
				*ptr = '_';
			}
		}
	}

	asprintf(&result, "%s%s", result, pd_value_to_string(pd.value));

	if (pd.uom != NULL) {
		asprintf(&result, "%s%s", result, pd.uom);
	}

	if (pd.warn_present) {
		asprintf(&result, "%s;%s", result, mp_range_to_string(pd.warn));
	} else {
		asprintf(&result, "%s;", result);
	}

	if (pd.crit_present) {
		asprintf(&result, "%s;%s", result, mp_range_to_string(pd.crit));
	} else {
		asprintf(&result, "%s;", result);
	}
	if (pd.min_present) {
		asprintf(&result, "%s;%s", result, pd_value_to_string(pd.min));
	} else {
		asprintf(&result, "%s;", result);
	}

	if (pd.max_present) {
		asprintf(&result, "%s;%s", result, pd_value_to_string(pd.max));
	}

	/*printf("pd_to_string: %s\n", result); */

	return result;
}

char *pd_list_to_string(const pd_list pd) {
	char *result = pd_to_string(pd.data);

	for (pd_list *elem = pd.next; elem != NULL; elem = elem->next) {
		asprintf(&result, "%s %s", result, pd_to_string(elem->data));
	}

	return result;
}

mp_perfdata perfdata_init() {
	mp_perfdata pd = {};
	return pd;
}

pd_list *pd_list_init() {
	pd_list *tmp = (pd_list *)calloc(1, sizeof(pd_list));
	if (tmp == NULL) {
		die(STATE_UNKNOWN, "calloc failed\n");
	}
	tmp->next = NULL;
	return tmp;
}

mp_range mp_range_init() {
	mp_range result = {
		.alert_on_inside_range = OUTSIDE,
		.start = {},
		.start_infinity = true,
		.end = {},
		.end_infinity = true,
	};

	return result;
}

mp_range mp_range_set_start(mp_range input, mp_perfdata_value perf_val) {
	input.start = perf_val;
	input.start_infinity = false;
	return input;
}

mp_range mp_range_set_end(mp_range input, mp_perfdata_value perf_val) {
	input.end = perf_val;
	input.end_infinity = false;
	return input;
}

void pd_list_append(pd_list pdl[1], const mp_perfdata pd) {
	assert(pdl != NULL);

	if (pdl->data.value.type == PD_TYPE_NONE) {
		// first entry is still empty
		pdl->data = pd;
	} else {
		// find last element in the list
		pd_list *curr = pdl;
		pd_list *next = pdl->next;

		while (next != NULL) {
			curr = next;
			next = next->next;
		}

		if (curr->data.value.type == PD_TYPE_NONE) {
			// still empty
			curr->data = pd;
		} else {
			// new a new one
			curr->next = pd_list_init();
			curr->next->data = pd;
		}
	}
}

void pd_list_free(pd_list pdl[1]) {
	while (pdl != NULL) {
		pd_list *old = pdl;
		pdl = pdl->next;
		free(old);
	}
}

/*
 * returns -1 if a < b, 0 if a == b, 1 if a > b
 */
int cmp_perfdata_value(const mp_perfdata_value a, const mp_perfdata_value b) {
	// Test if types are different
	if (a.type == b.type) {

		switch (a.type) {
		case PD_TYPE_UINT:
			if (a.pd_uint < b.pd_uint) {
				return -1;
			} else if (a.pd_uint == b.pd_uint) {
				return 0;
			} else {
				return 1;
			}
			break;
		case PD_TYPE_INT:
			if (a.pd_int < b.pd_int) {
				return -1;
			} else if (a.pd_int == b.pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		case PD_TYPE_DOUBLE:
			if (a.pd_int < b.pd_int) {
				return -1;
			} else if (a.pd_int == b.pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		default:
			die(STATE_UNKNOWN, "Error in %s line: %d!", __FILE__, __LINE__);
		}
	}

	// Get dirty here
	long double floating_a = 0;

	switch (a.type) {
	case PD_TYPE_UINT:
		floating_a = a.pd_uint;
		break;
	case PD_TYPE_INT:
		floating_a = a.pd_int;
		break;
	case PD_TYPE_DOUBLE:
		floating_a = a.pd_double;
		break;
	default:
		die(STATE_UNKNOWN, "Error in %s line: %d!", __FILE__, __LINE__);
	}

	long double floating_b = 0;
	switch (b.type) {
	case PD_TYPE_UINT:
		floating_b = b.pd_uint;
		break;
	case PD_TYPE_INT:
		floating_b = b.pd_int;
		break;
	case PD_TYPE_DOUBLE:
		floating_b = b.pd_double;
		break;
	default:
		die(STATE_UNKNOWN, "Error in %s line: %d!", __FILE__, __LINE__);
	}

	if (floating_a < floating_b) {
		return -1;
	}
	if (floating_a == floating_b) {
		return 0;
	}
	return 1;
}

char *mp_range_to_string(const mp_range input) {
	char *result = "";
	if (input.alert_on_inside_range == INSIDE) {
		asprintf(&result, "@");
	}

	if (input.start_infinity) {
		asprintf(&result, "%s~:", result);
	} else {
		asprintf(&result, "%s%s:", result, pd_value_to_string(input.start));
	}

	if (!input.end_infinity) {
		asprintf(&result, "%s%s", result, pd_value_to_string(input.end));
	}
	return result;
}

mp_perfdata mp_set_pd_value_float(mp_perfdata pd, float value) {
	return mp_set_pd_value_double(pd, value);
}

mp_perfdata mp_set_pd_value_double(mp_perfdata pd, double value) {
	pd.value.pd_double = value;
	pd.value.type = PD_TYPE_DOUBLE;
	return pd;
}

mp_perfdata mp_set_pd_value_char(mp_perfdata pd, char value) {
	return mp_set_pd_value_long_long(pd, (long long)value);
}

mp_perfdata mp_set_pd_value_u_char(mp_perfdata pd, unsigned char value) {
	return mp_set_pd_value_u_long_long(pd, (unsigned long long)value);
}

mp_perfdata mp_set_pd_value_int(mp_perfdata pd, int value) {
	return mp_set_pd_value_long_long(pd, (long long)value);
}

mp_perfdata mp_set_pd_value_u_int(mp_perfdata pd, unsigned int value) {
	return mp_set_pd_value_u_long_long(pd, (unsigned long long)value);
}

mp_perfdata mp_set_pd_value_long(mp_perfdata pd, long value) {
	return mp_set_pd_value_long_long(pd, (long long)value);
}

mp_perfdata mp_set_pd_value_u_long(mp_perfdata pd, unsigned long value) {
	return mp_set_pd_value_u_long_long(pd, (unsigned long long)value);
}

mp_perfdata mp_set_pd_value_long_long(mp_perfdata pd, long long value) {
	pd.value.pd_int = value;
	pd.value.type = PD_TYPE_INT;
	return pd;
}

mp_perfdata mp_set_pd_value_u_long_long(mp_perfdata pd, unsigned long long value) {
	pd.value.pd_uint = value;
	pd.value.type = PD_TYPE_UINT;
	return pd;
}

mp_perfdata_value mp_create_pd_value_double(double value) {
	mp_perfdata_value res = {0};
	res.type = PD_TYPE_DOUBLE;
	res.pd_double = value;
	return res;
}

mp_perfdata_value mp_create_pd_value_float(float value) {
	return mp_create_pd_value_double((double)value);
}

mp_perfdata_value mp_create_pd_value_char(char value) {
	return mp_create_pd_value_long_long((long long)value);
}

mp_perfdata_value mp_create_pd_value_u_char(unsigned char value) {
	return mp_create_pd_value_u_long_long((unsigned long long)value);
}

mp_perfdata_value mp_create_pd_value_int(int value) {
	return mp_create_pd_value_long_long((long long)value);
}

mp_perfdata_value mp_create_pd_value_u_int(unsigned int value) {
	return mp_create_pd_value_u_long_long((unsigned long long)value);
}

mp_perfdata_value mp_create_pd_value_long(long value) {
	return mp_create_pd_value_long_long((long long)value);
}

mp_perfdata_value mp_create_pd_value_u_long(unsigned long value) {
	return mp_create_pd_value_u_long_long((unsigned long long)value);
}

mp_perfdata_value mp_create_pd_value_long_long(long long value) {
	mp_perfdata_value res = {0};
	res.type = PD_TYPE_INT;
	res.pd_int = value;
	return res;
}

mp_perfdata_value mp_create_pd_value_u_long_long(unsigned long long value) {
	mp_perfdata_value res = {0};
	res.type = PD_TYPE_UINT;
	res.pd_uint = value;
	return res;
}

char *fmt_range(range foo) { return foo.text; }

typedef struct integer_parser_wrapper {
	int error;
	mp_perfdata_value value;
} integer_parser_wrapper;

typedef struct double_parser_wrapper {
	int error;
	mp_perfdata_value value;
} double_parser_wrapper;

typedef struct perfdata_value_parser_wrapper {
	int error;
	mp_perfdata_value value;
} perfdata_value_parser_wrapper;

double_parser_wrapper parse_double(const char *input);
integer_parser_wrapper parse_integer(const char *input);
perfdata_value_parser_wrapper parse_pd_value(const char *input);

mp_range_parsed mp_parse_range_string(const char *input) {
	if (input == NULL) {
		mp_range_parsed result = {
			.error = MP_RANGE_PARSING_FAILURE,
		};
		return result;
	}

	if (strlen(input) == 0) {
		mp_range_parsed result = {
			.error = MP_RANGE_PARSING_FAILURE,
		};
		return result;
	}

	mp_range_parsed result = {
		.range = mp_range_init(),
		.error = MP_PARSING_SUCCES,
	};

	if (input[0] == '@') {
		// found an '@' at beginning, so invert the range logic
		result.range.alert_on_inside_range = INSIDE;

		// advance the pointer one symbol
		input++;
	}

	char *working_copy = strdup(input);
	if (working_copy == NULL) {
		// strdup error, probably
		mp_range_parsed result = {
			.error = MP_RANGE_PARSING_FAILURE,
		};
		return result;
	}
	input = working_copy;

	char *separator = index(working_copy, ':');
	if (separator != NULL) {
		// Found a separator
		// set the separator to 0, so we have two different strings
		*separator = '\0';

		if (input[0] == '~') {
			// the beginning starts with '~', so it might be infinity
			if (&input[1] != separator) {
				// the next symbol after '~' is not the separator!
				// so input is probably wrong
				result.error = MP_RANGE_PARSING_FAILURE;
				free(working_copy);
				return result;
			}

			result.range.start_infinity = true;
		} else {
			// No '~' at the beginning, so this should be a number
			result.range.start_infinity = false;
			perfdata_value_parser_wrapper parsed_pd = parse_pd_value(input);

			if (parsed_pd.error != MP_PARSING_SUCCES) {
				result.error = parsed_pd.error;
				free(working_copy);
				return result;
			}

			result.range.start = parsed_pd.value;
			result.range.start_infinity = false;
		}
		// got the first part now
		// advance the pointer
		input = separator + 1;
	}

	// End part or no separator
	if (input[0] == '\0') {
		// the end is infinite
		result.range.end_infinity = true;
	} else {
		perfdata_value_parser_wrapper parsed_pd = parse_pd_value(input);

		if (parsed_pd.error != MP_PARSING_SUCCES) {
			result.error = parsed_pd.error;
			return result;
		}
		result.range.end = parsed_pd.value;
		result.range.end_infinity = false;
	}
	free(working_copy);
	return result;
}

double_parser_wrapper parse_double(const char *input) {
	double_parser_wrapper result = {
		.error = MP_PARSING_SUCCES,
	};

	if (input == NULL) {
		result.error = MP_PARSING_FAILURE;
		return result;
	}

	char *endptr = NULL;
	errno = 0;
	double tmp = strtod(input, &endptr);

	if (input == endptr) {
		// man 3 strtod says, no conversion performed
		result.error = MP_PARSING_FAILURE;
		return result;
	}

	if (errno) {
		// some other error
		// TODO maybe differentiate a little bit
		result.error = MP_PARSING_FAILURE;
		return result;
	}

	result.value = mp_create_pd_value(tmp);
	return result;
}

integer_parser_wrapper parse_integer(const char *input) {
	integer_parser_wrapper result = {
		.error = MP_PARSING_SUCCES,
	};

	if (input == NULL) {
		result.error = MP_PARSING_FAILURE;
		return result;
	}

	char *endptr = NULL;
	errno = 0;
	long long tmp = strtoll(input, &endptr, 0);

	// validating *sigh*
	if (*endptr != '\0') {
		// something went wrong in strtoll
		if (tmp == LLONG_MIN) {
			// underflow
			result.error = MP_RANGE_PARSING_UNDERFLOW;
			return result;
		}

		if (tmp == LLONG_MAX) {
			// overflow
			result.error = MP_RANGE_PARSING_OVERFLOW;
			return result;
		}

		// still wrong, but not sure why, probably invalid characters
		if (errno == EINVAL) {
			result.error = MP_RANGE_PARSING_INVALID_CHAR;
			return result;
		}

		// some other error, do catch all here
		result.error = MP_RANGE_PARSING_FAILURE;
		return result;
	}

	// no error, should be fine
	result.value = mp_create_pd_value(tmp);
	return result;
}

perfdata_value_parser_wrapper parse_pd_value(const char *input) {
	// try integer first
	integer_parser_wrapper tmp_int = parse_integer(input);

	if (tmp_int.error == MP_PARSING_SUCCES) {
		perfdata_value_parser_wrapper result = {
			.error = tmp_int.error,
			.value = tmp_int.value,
		};
		return result;
	}

	double_parser_wrapper tmp_double = parse_double(input);
	perfdata_value_parser_wrapper result = {};
	if (tmp_double.error == MP_PARSING_SUCCES) {
		result.error = tmp_double.error;
		result.value = tmp_double.value;
	} else {
		result.error = tmp_double.error;
	}
	return result;
}

mp_perfdata mp_set_pd_max_value(mp_perfdata perfdata, mp_perfdata_value value) {
	perfdata.max = value;
	perfdata.max_present = true;
	return perfdata;
}

mp_perfdata mp_set_pd_min_value(mp_perfdata perfdata, mp_perfdata_value value) {
	perfdata.min = value;
	perfdata.min_present = true;
	return perfdata;
}

double mp_get_pd_value(mp_perfdata_value value) {
	assert(value.type != PD_TYPE_NONE);
	switch (value.type) {
	case PD_TYPE_DOUBLE:
		return value.pd_double;
	case PD_TYPE_INT:
		return (double)value.pd_int;
	case PD_TYPE_UINT:
		return (double)value.pd_uint;
	default:
		return 0; // just to make the compiler happy
	}
}

mp_perfdata_value mp_pd_value_multiply(mp_perfdata_value left, mp_perfdata_value right) {
	if (left.type == right.type) {
		switch (left.type) {
		case PD_TYPE_DOUBLE:
			left.pd_double *= right.pd_double;
			return left;
		case PD_TYPE_INT:
			left.pd_int *= right.pd_int;
			return left;
		case PD_TYPE_UINT:
			left.pd_uint *= right.pd_uint;
			return left;
		default:
			// what to here?
			return left;
		}
	}

	// Different types, oh boy, just do the lazy thing for now and switch to double
	switch (left.type) {
	case PD_TYPE_INT:
		left.pd_double = (double)left.pd_int;
		left.type = PD_TYPE_DOUBLE;
		break;
	case PD_TYPE_UINT:
		left.pd_double = (double)left.pd_uint;
		left.type = PD_TYPE_DOUBLE;
		break;
	}

	switch (right.type) {
	case PD_TYPE_INT:
		right.pd_double = (double)right.pd_int;
		right.type = PD_TYPE_DOUBLE;
		break;
	case PD_TYPE_UINT:
		right.pd_double = (double)right.pd_uint;
		right.type = PD_TYPE_DOUBLE;
		break;
	}

	left.pd_double *= right.pd_double;
	return left;
}

mp_range mp_range_multiply(mp_range range, mp_perfdata_value factor) {
	if (!range.end_infinity) {
		range.end = mp_pd_value_multiply(range.end, factor);
	}
	if (!range.start_infinity) {
		range.start = mp_pd_value_multiply(range.start, factor);
	}
	return range;
}
