#pragma once

#include "./config.h"

check_snmp_test_unit check_snmp_test_unit_init();
int check_snmp_set_thresholds(const char *, check_snmp_test_unit[], size_t, bool);
check_snmp_config check_snmp_config_init();
