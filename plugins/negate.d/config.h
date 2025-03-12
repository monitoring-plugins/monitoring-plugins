#pragma once

#include "states.h"

typedef struct {
	mp_state_enum state[4];
	bool subst_text;
	char **command_line;
} negate_config;

negate_config negate_config_init() {
	negate_config tmp = {
		.state =
			{
				STATE_OK,
				STATE_WARNING,
				STATE_CRITICAL,
				STATE_UNKNOWN,
			},
		.subst_text = false,
		.command_line = NULL,
	};
	return tmp;
}
