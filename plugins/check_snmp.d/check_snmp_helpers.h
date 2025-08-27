#pragma once

#include "./config.h"

check_snmp_test_unit check_snmp_test_unit_init();
int check_snmp_set_thresholds(char *threshold_string, check_snmp_test_unit tu[],
							  size_t max_test_units, bool is_critical);
check_snmp_config check_snmp_config_init();
