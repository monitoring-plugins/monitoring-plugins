#pragma once

#include "./config.h"
#include <net-snmp/library/asn1.h>

check_snmp_test_unit check_snmp_test_unit_init();
int check_snmp_set_thresholds(const char *, check_snmp_test_unit[], size_t, bool);
check_snmp_config check_snmp_config_init();

typedef struct {
	oid oid[MAX_OID_LEN];
	size_t oid_length;
	unsigned char type;
	union {
		uint64_t uIntVal;
		int64_t intVal;
		double doubleVal;
	} value;
	char *string_response;
} response_value;

typedef struct {
	int errorcode;
	response_value *response_values;
} snmp_responces;
snmp_responces do_snmp_query(check_snmp_config_snmp_parameters parameters);

// state is similar to response, but only numerics and a timestamp
typedef struct {
	time_t timestamp;
	oid oid[MAX_OID_LEN];
	size_t oid_length;
	unsigned char type;
	union {
		unsigned long long uIntVal;
		long long intVal;
		double doubleVal;
	} value;
} check_snmp_state_entry;

typedef struct {
	check_snmp_state_entry state;
	mp_subcheck sc;
} check_snmp_evaluation;
check_snmp_evaluation evaluate_single_unit(response_value response,
										   check_snmp_evaluation_parameters eval_params,
										   check_snmp_test_unit test_unit, time_t query_timestamp,
										   check_snmp_state_entry prev_state,
										   bool have_previous_state);

#define NP_STATE_FORMAT_VERSION 1

typedef struct state_data_struct {
	time_t time;
	void *data;
	size_t length; /* Of binary data */
	int errorcode;
} state_data;

typedef struct state_key_struct {
	char *name;
	char *plugin_name;
	int data_version;
	char *_filename;
	state_data *state_data;
} state_key;

state_data *np_state_read(state_key stateKey);
state_key np_enable_state(char *keyname, int expected_data_version, const char *plugin_name,
						  int argc, char **argv);
void np_state_write_string(state_key stateKey, time_t timestamp, char *stringToStore);
