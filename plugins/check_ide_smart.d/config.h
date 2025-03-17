#pragma once

#include "../../config.h"
#include <stddef.h>

typedef struct {
	char *device;
} check_ide_smart_config;

check_ide_smart_config check_ide_smart_init() {
	check_ide_smart_config tmp = {
		.device = NULL,
	};
	return tmp;
}
