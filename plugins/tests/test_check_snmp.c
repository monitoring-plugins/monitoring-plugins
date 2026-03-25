/*****************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

#include "../../tap/tap.h"
#include "../../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <ftw.h>

#include "../check_snmp.d/check_snmp_helpers.h"
#include "states.h"

// helpers
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
	(void)sb;
	(void)typeflag;
	(void)ftwbuf;

	int rv = remove(fpath);

	if (rv) {
		perror(fpath);
	}

	return rv;
}

int rmrf(char *path) { return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS); }

char *create_file_path(const char *prefix, const char *end_part) {
	char *result = calloc(strlen(prefix) + strlen(end_part) + 1, sizeof(char));
	if (result == NULL) {
		die(2, "Failed to allocate memory");
	}

	strcpy(result, prefix);
	strcat(result, end_part);
	return result;
}

// declarations to make it compile
int verbose = 0;
void print_usage(void) {}
const char *progname = "test_check_snmp";

char *_np_state_generate_key(int argc, char **argv);
char *_np_state_calculate_location_prefix(void);
//

void test_key_generation(int argc, char **argv) {
	char *temp_string = (char *)_np_state_generate_key(argc, argv);
	ok(!strcmp(temp_string, "8a5881f5f97e68878b738538d9b864e1c8e3e463"),
	   "Got hash with exe and no parameters: %s", temp_string);

	if (!strcmp(temp_string, "8dd4ba3c1dcea40bd80fe2e2c73872b669e211banot")) {
		diag("You are probably running in wrong directory. Must run as ./tests/test_check_snmp");
	}

	int fake_argc = 4;
	char *fake_argv[] = {
		"./tests/test_check_snmp",
		"here",
		"--and",
		"now",
	};
	temp_string = (char *)_np_state_generate_key(fake_argc, fake_argv);
	ok(!strcmp(temp_string, "cf15afca3c3a45d60056384f64459880ce3dedc5"),
	   "Got %s based on expected argv", temp_string);
}

void test_state_location_prefix(void) {
	unsetenv("MP_STATE_PATH");
	char *temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory");

	setenv("MP_STATE_PATH", "", 1);
	temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory even with empty string");

	setenv("MP_STATE_PATH", "/usr/local/nagios/var", 1);
	temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, "/usr/local/nagios/var"), "Got default directory");

	unsetenv("MP_STATE_PATH");
}

void test_enable_state(void) {
	const int fake_argc = 1;
	const char *fake_argv[1] = {"fake_argv"};
	state_key temp_state_key1 = np_enable_state(NULL, 51, "check_test", fake_argc, fake_argv);
	ok(!strcmp(temp_state_key1.plugin_name, "check_test"), "Got plugin name");
	ok(!strcmp(temp_state_key1.name, "8dd4ba3c1dcea40bd80fe2e2c73872b669e211ba"),
	   "Got generated filename: %s", temp_state_key1.name);

	const char *expected_plugin_name = "check_foobar";
	state_key temp_state_key2 =
		np_enable_state("allowedchars_in_keyname", 77, expected_plugin_name, fake_argc, fake_argv);
	ok(!strcmp(temp_state_key2.plugin_name, expected_plugin_name), "Got plugin name: %s",
	   temp_state_key2.plugin_name);
	ok(!strcmp(temp_state_key2.name, "allowedchars_in_keyname"),
	   "Got key name with valid chars: %s", temp_state_key2.name);
}

void test_read_state(state_key test_state, const char test_string[], const char *test_dir,
					 const char *example_dir) {
	np_state_read_wrapper recovered_state = np_state_read(test_state);
	ok(recovered_state.errorcode == 0, "Got state data now");

	if (recovered_state.errorcode != 0) {
		diag("Are you running in right directory? No state data could be recovered");
		exit(2);
	}

	ok(recovered_state.data.time == 1234567890, "Got time: %d", recovered_state.data.time);
	ok(!strcmp((char *)recovered_state.data.data, test_string), "Data as expected");

	test_state.data_version = 53;
	recovered_state = np_state_read(test_state);
	ok(recovered_state.errorcode != 0, "Older data version gives error");
	test_state.data_version = 54;

	char *nonexistent_file_path = create_file_path(example_dir, "/nonexistent");
	test_state._filename = nonexistent_file_path;
	np_state_read_wrapper non_existent = np_state_read(test_state);
	ok(non_existent.errorcode != 0, "Missing file gives error");

	char *oldformat_file_path = create_file_path(example_dir, "/oldformat");
	test_state._filename = oldformat_file_path;
	np_state_read_wrapper old_format = np_state_read(test_state);
	ok(old_format.errorcode != 0, "Old file format gives error");

	char *baddate_file_path = create_file_path(example_dir, "/baddate");
	test_state._filename = baddate_file_path;
	np_state_read_wrapper baddate = np_state_read(test_state);
	ok(baddate.errorcode != 0, "Bad date gives error");

	char *missingdataline_file_path = create_file_path(example_dir, "/missingdataline");
	test_state._filename = missingdataline_file_path;
	np_state_read_wrapper missingdataline = np_state_read(test_state);
	ok(missingdataline.errorcode != 0, "Missing data line gives error");

	// generate new file to test time proceeding
	char *generated_file_path = create_file_path(test_dir, "/generated");
	test_state._filename = generated_file_path;

	char *example_statefile_file_path = create_file_path(example_dir, "/statefile");

	time_t current_time = 1234567890;
	np_state_write_string(test_state, current_time, test_string);
	char *cmp_execution_string = NULL;
	(void) asprintf(&cmp_execution_string, "cmp %s %s", generated_file_path, example_statefile_file_path);
	ok(system(cmp_execution_string) == 0, "Generated file same as expected");

	char *generated_dir_test_file_path =
		create_file_path(test_dir, "/generated_directory/statefile");
	test_state._filename = generated_dir_test_file_path;
	current_time = 1234567890;
	np_state_write_string(test_state, current_time, test_string);
	(void) asprintf(&cmp_execution_string, "cmp %s %s", generated_dir_test_file_path,
			 example_statefile_file_path);
	ok(system(cmp_execution_string) == 0, "Have created directory");

	test_state._filename = generated_dir_test_file_path;
	np_state_write_string(test_state, 0, test_string);
	np_state_read_wrapper recovered_state_1 = np_state_read(test_state);
	ok(recovered_state_1.errorcode == 0, "recovered state succesfully");
	/* Check time is set to current_time */
	(void) asprintf(&cmp_execution_string, "cmp %s %s > /dev/null", generated_dir_test_file_path,
			 example_statefile_file_path);
	ok(system(cmp_execution_string) != 0, "Generated file should be different this time");

	time(&current_time);
	ok(recovered_state_1.data.time - current_time <= 1, "Has time generated from current time");

	/* Don't know how to automatically test this. Need to be able to redefine die and catch the
	 * error */
	/*
	temp_state_key->_filename="/dev/do/not/expect/to/be/able/to/write";
	np_state_write_string(0, "Bad file");
	*/
}

int main(int argc, char **argv) {
	// Generate test directory
	char *base_path = dirname(argv[0]);
	const char test_dir_name[] = "/test";

	char *test_dir_path = create_file_path(base_path, test_dir_name);

	// remove test directory before using it
	rmrf(test_dir_path);

	int result_mkdir = mkdir(test_dir_path, 0700);

	ok(result_mkdir == 0, "Generated test directory: %s", test_dir_path);

	if (result_mkdir != 0) {
		// failed to create test directory, rest is useless
		diag("Failed to generate test directory. Aborting here. mkdir result was: %s",
			 strerror(errno));
		exit(2);
	}

	test_key_generation(argc, argv);

	test_state_location_prefix();

	int fake_argc = 1;
	const char *fake_argv[1] = {"fake_argv"};
	const char *expected_plugin_name = "fake_pluginname";
	const char *test_state_subpath = "/test_state";
	char *test_state_path = create_file_path(test_dir_path, test_state_subpath);
	setenv("MP_STATE_PATH", test_state_path, 1);

	state_key temp_state =
		np_enable_state("funnykeyname", 54, expected_plugin_name, fake_argc, fake_argv);
	const char *test_string = "String to read";
	np_state_write_string(temp_state, 1234567890, test_string);

	ok(!strcmp(temp_state.plugin_name, expected_plugin_name), "Got plugin name: %s",
	   temp_state.plugin_name);
	ok(!strcmp(temp_state.name, "funnykeyname"), "Got key name");

	np_state_read_wrapper recoverd_state_data = np_state_read(temp_state);
	ok(recoverd_state_data.errorcode == 0, "Retrieve state data from file '%s'",
	   temp_state._filename);

	const char *example_dir = "/var/check_snmp";
	char *example_path = create_file_path(base_path, example_dir);
	test_read_state(temp_state, test_string, test_dir_path, example_path);

	np_cleanup();

	// remove test directory after using it
	// rmrf(test_dir_path);
}
