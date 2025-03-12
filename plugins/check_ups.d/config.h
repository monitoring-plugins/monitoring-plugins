#pragma once

#include "../../config.h"
#include <stddef.h>

#define UPS_NONE      0  /* no supported options */
#define UPS_UTILITY   1  /* supports utility line    */
#define UPS_BATTPCT   2  /* supports percent battery remaining */
#define UPS_STATUS    4  /* supports UPS status */
#define UPS_TEMP      8  /* supports UPS temperature */
#define UPS_LOADPCT   16 /* supports load percent */
#define UPS_REALPOWER 32 /* supports real power */

#define UPSSTATUS_NONE    0
#define UPSSTATUS_OFF     1
#define UPSSTATUS_OL      2
#define UPSSTATUS_OB      4
#define UPSSTATUS_LB      8
#define UPSSTATUS_CAL     16
#define UPSSTATUS_RB      32 /*Replace Battery */
#define UPSSTATUS_BYPASS  64
#define UPSSTATUS_OVER    128
#define UPSSTATUS_TRIM    256
#define UPSSTATUS_BOOST   512
#define UPSSTATUS_CHRG    1024
#define UPSSTATUS_DISCHRG 2048
#define UPSSTATUS_UNKNOWN 4096
#define UPSSTATUS_ALARM   8192

enum {
	PORT = 3493
};

typedef struct ups_config {
	unsigned int server_port;
	char *server_address;
	char *ups_name;
	double warning_value;
	double critical_value;
	bool check_warn;
	bool check_crit;
	int check_variable;
	bool temp_output_c;
} check_ups_config;

check_ups_config check_ups_config_init(void) {
	check_ups_config tmp = {0};
	tmp.server_port = PORT;
	tmp.server_address = NULL;
	tmp.ups_name = NULL;
	tmp.check_variable = UPS_NONE;

	return tmp;
}

