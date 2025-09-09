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

#include "tap.h"
#include "../../config.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utils_base.c"
#include "../check_snmp.d/check_snmp_helpers.h"

char *_np_state_generate_key(int argc, char **argv);
char *_np_state_calculate_location_prefix(void);

int main(int argc, char **argv) {
	char *temp_string = (char *)_np_state_generate_key(argc, argv);
	ok(!strcmp(temp_string, "e2d17f995fd4c020411b85e3e3d0ff7306d4147e"),
	   "Got hash with exe and no parameters") ||
		diag("You are probably running in wrong directory. Must run as ./test_utils");

	int fake_argc = 4;
	char *fake_argv[] = {
		"./test_utils",
		"here",
		"--and",
		"now",
	};
	temp_string = (char *)_np_state_generate_key(fake_argc, fake_argv);
	ok(!strcmp(temp_string, "bd72da9f78ff1419fad921ea5e43ce56508aef6c"),
	   "Got based on expected argv");

	unsetenv("MP_STATE_PATH");
	temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory");

	setenv("MP_STATE_PATH", "", 1);
	temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory even with empty string");

	setenv("MP_STATE_PATH", "/usr/local/nagios/var", 1);
	temp_string = (char *)_np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, "/usr/local/nagios/var"), "Got default directory");

	fake_argc = 1;
	fake_argv[0] = "./test_utils";
	state_key temp_state_key1 = np_enable_state(NULL, 51, "check_test", fake_argc, fake_argv);
	ok(!strcmp(temp_state_key1.plugin_name, "check_test"), "Got plugin name");
	ok(!strcmp(temp_state_key1.name, "e2d17f995fd4c020411b85e3e3d0ff7306d4147e"),
	   "Got generated filename");

	state_key temp_state_key2 =
		np_enable_state("allowedchars_in_keyname", 77, "check_snmp", fake_argc, fake_argv);

	char state_path[1024];
	sprintf(state_path, "/usr/local/nagios/var/%lu/check_test/allowedchars_in_keyname",
			(unsigned long)geteuid());
	ok(!strcmp(temp_state_key2.plugin_name, "check_test"), "Got plugin name");
	ok(!strcmp(temp_state_key2.name, "allowedchars_in_keyname"), "Got key name with valid chars");
	ok(!strcmp(temp_state_key2._filename, state_path), "Got internal filename");

	/* Don't do this test just yet. Will die */
	/*
	np_enable_state("bad^chars$in@here", 77);
	temp_state_key = this_monitoring_plugin->state;
	ok( !strcmp(temp_state_key->name, "bad_chars_in_here"), "Got key name with bad chars replaced"
	);
	*/

	state_key temp_state_key3 =
		np_enable_state("funnykeyname", 54, "check_snmp", fake_argc, fake_argv);
	sprintf(state_path, "/usr/local/nagios/var/%lu/check_test/funnykeyname",
			(unsigned long)geteuid());
	ok(!strcmp(temp_state_key3.plugin_name, "check_test"), "Got plugin name");
	ok(!strcmp(temp_state_key3.name, "funnykeyname"), "Got key name");

	ok(!strcmp(temp_state_key3._filename, state_path), "Got internal filename");
	ok(temp_state_key3.data_version == 54, "Version set");

	state_data *temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Got no state data as file does not exist");

	/*
		temp_fp = fopen("var/statefile", "r");
		if (temp_fp==NULL)
			printf("Error opening. errno=%d\n", errno);
		printf("temp_fp=%s\n", temp_fp);
		ok( _np_state_read_file(temp_fp) == true, "Can read state file" );
		fclose(temp_fp);
	*/

	temp_state_key3._filename = "var/statefile";
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data != NULL, "Got state data now") ||
		diag("Are you running in right directory? Will get coredump next if not");
	ok(temp_state_data->time == 1234567890, "Got time");
	ok(!strcmp((char *)temp_state_data->data, "String to read"), "Data as expected");

	temp_state_key3.data_version = 53;
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Older data version gives NULL");
	temp_state_key3.data_version = 54;

	temp_state_key3._filename = "var/nonexistent";
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Missing file gives NULL");

	temp_state_key3._filename = "var/oldformat";
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Old file format gives NULL");

	temp_state_key3._filename = "var/baddate";
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Bad date gives NULL");

	temp_state_key3._filename = "var/missingdataline";
	temp_state_data = np_state_read(temp_state_key3);
	ok(temp_state_data == NULL, "Missing data line gives NULL");

	unlink("var/generated");
	temp_state_key3._filename = "var/generated";

	time_t current_time = 1234567890;
	np_state_write_string(temp_state_key3, current_time, "String to read");
	ok(system("cmp var/generated var/statefile") == 0, "Generated file same as expected");

	unlink("var/generated_directory/statefile");
	unlink("var/generated_directory");
	temp_state_key3._filename = "var/generated_directory/statefile";
	current_time = 1234567890;
	np_state_write_string(temp_state_key3, current_time, "String to read");
	ok(system("cmp var/generated_directory/statefile var/statefile") == 0,
	   "Have created directory");

	/* This test to check cannot write to dir - can't automate yet */
	/*
	unlink("var/generated_bad_dir");
	mkdir("var/generated_bad_dir", S_IRUSR);
	np_state_write_string(current_time, "String to read");
	*/

	temp_state_key3._filename = "var/generated";
	time(&current_time);
	np_state_write_string(temp_state_key3, 0, "String to read");
	temp_state_data = np_state_read(temp_state_key3);
	/* Check time is set to current_time */
	ok(system("cmp var/generated var/statefile > /dev/null") != 0,
	   "Generated file should be different this time");
	ok(temp_state_data->time - current_time <= 1, "Has time generated from current time");

	/* Don't know how to automatically test this. Need to be able to redefine die and catch the
	 * error */
	/*
	temp_state_key->_filename="/dev/do/not/expect/to/be/able/to/write";
	np_state_write_string(0, "Bad file");
	*/

	np_cleanup();
}
