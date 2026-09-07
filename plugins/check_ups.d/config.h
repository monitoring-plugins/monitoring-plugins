#pragma once

#include "../../config.h"
#include "thresholds.h"
#include <stddef.h>

typedef enum {
	UPS_NONE = 0,       /* no supported options */
	UPS_UTILITY = 1,    /* supports utility line    */
	UPS_BATTPCT = 2,    /* supports percent battery remaining */
	UPS_STATUS = 4,     /* supports UPS status */
	UPS_TEMP = 8,       /* supports UPS temperature */
	UPS_LOADPCT = 16,   /* supports load percent */
	UPS_REALPOWER = 32, /* supports real power */
} ups_test_type;

typedef enum {
	UPSSTATUS_NONE = 0,
	UPSSTATUS_OFF = 1,
	UPSSTATUS_OL = 2,
	UPSSTATUS_OB = 4,
	UPSSTATUS_LB = 8,
	UPSSTATUS_CAL = 16,
	UPSSTATUS_RB = 32, /*Replace Battery */
	UPSSTATUS_BYPASS = 64,
	UPSSTATUS_OVER = 128,
	UPSSTATUS_TRIM = 256,
	UPSSTATUS_BOOST = 512,
	UPSSTATUS_CHRG = 1024,
	UPSSTATUS_DISCHRG = 2048,
	UPSSTATUS_UNKNOWN = 4096,
	UPSSTATUS_ALARM = 8192,
} ups_status_type;

enum {
	PORT = 3493
};

typedef struct ups_config {
	unsigned int server_port;
	char *server_address;
	char *ups_name;

	mp_thresholds utility_thresholds;
	mp_thresholds battery_thresholds;
	mp_thresholds load_thresholds;
	mp_thresholds real_power_thresholds;
	mp_thresholds temperature_thresholds;

	bool temp_output_c;
} check_ups_config;

check_ups_config check_ups_config_init(void) {
	check_ups_config tmp = {
		.server_port = PORT,
		.server_address = NULL,
		.ups_name = NULL,

		.utility_thresholds = mp_thresholds_init(),
		.battery_thresholds = mp_thresholds_init(),
		.load_thresholds = mp_thresholds_init(),
		.real_power_thresholds = mp_thresholds_init(),
		.temperature_thresholds = mp_thresholds_init(),
	};

	return tmp;
}
