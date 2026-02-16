#ifndef _MP_STATES_
#define _MP_STATES_

#include "../config.h"
#include <sys/param.h>

typedef enum state_enum {
	STATE_OK,
	STATE_WARNING,
	STATE_CRITICAL,
	STATE_UNKNOWN,
	STATE_DEPENDENT
} mp_state_enum;

/* **************************************************************************
 * max_state(STATE_x, STATE_y)
 * compares STATE_x to  STATE_y and returns result based on the following
 * STATE_UNKNOWN < STATE_OK < STATE_WARNING < STATE_CRITICAL
 *
 * Note that numerically the above does not hold
 ****************************************************************************/

static inline mp_state_enum max_state(mp_state_enum a, mp_state_enum b) {
	if (a == STATE_CRITICAL || b == STATE_CRITICAL) {
		return STATE_CRITICAL;
	}
	if (a == STATE_WARNING || b == STATE_WARNING) {
		return STATE_WARNING;
	}
	if (a == STATE_OK || b == STATE_OK) {
		return STATE_OK;
	}
	if (a == STATE_UNKNOWN || b == STATE_UNKNOWN) {
		return STATE_UNKNOWN;
	}
	if (a == STATE_DEPENDENT || b == STATE_DEPENDENT) {
		return STATE_DEPENDENT;
	}
	return MAX(a, b);
}

/* **************************************************************************
 * max_state_alt(STATE_x, STATE_y)
 * compares STATE_x to  STATE_y and returns result based on the following
 * STATE_OK < STATE_DEPENDENT < STATE_UNKNOWN < STATE_WARNING < STATE_CRITICAL
 *
 * The main difference between max_state_alt and max_state it that it doesn't
 * allow setting a default to UNKNOWN. It will instead prioritize any valid
 * non-OK state.
 ****************************************************************************/

static inline mp_state_enum max_state_alt(mp_state_enum a, mp_state_enum b) {
	if (a == STATE_CRITICAL || b == STATE_CRITICAL) {
		return STATE_CRITICAL;
	}
	if (a == STATE_WARNING || b == STATE_WARNING) {
		return STATE_WARNING;
	}
	if (a == STATE_UNKNOWN || b == STATE_UNKNOWN) {
		return STATE_UNKNOWN;
	}
	if (a == STATE_DEPENDENT || b == STATE_DEPENDENT) {
		return STATE_DEPENDENT;
	}
	if (a == STATE_OK || b == STATE_OK) {
		return STATE_OK;
	}
	return MAX(a, b);
}

#endif
