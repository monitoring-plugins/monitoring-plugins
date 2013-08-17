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

#include "common.h"
#include "utils_base.h"

#include "tap.h"

#include <sys/types.h>
#include <sys/stat.h>

#include "utils_base.c"

int
main (int argc, char **argv)
{
	range	*range;
	double	temp;
	thresholds *thresholds = NULL;
	int	rc;
	char	*temp_string;
	state_key *temp_state_key = NULL;
	state_data *temp_state_data;
	time_t	current_time;

	plan_tests(150);

	ok( this_nagios_plugin==NULL, "nagios_plugin not initialised");

	np_init( "check_test", argc, argv );

	ok( this_nagios_plugin!=NULL, "nagios_plugin now initialised");
	ok( !strcmp(this_nagios_plugin->plugin_name, "check_test"), "plugin name initialised" );

	ok( this_nagios_plugin->argc==argc, "Argc set" );
	ok( this_nagios_plugin->argv==argv, "Argv set" );

	np_set_args(0,0);

	ok( this_nagios_plugin->argc==0, "argc changed" );
	ok( this_nagios_plugin->argv==0, "argv changed" );

	np_set_args(argc, argv);

	range = parse_range_string("6");
	ok( range != NULL, "'6' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("1:12%%");
	ok( range != NULL, "'1:12%%' is valid - percentages are ignored");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 12, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("-7:23");
	ok( range != NULL, "'-7:23' is valid range");
	ok( range->start == -7, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 23, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string(":5.75");
	ok( range != NULL, "':5.75' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 5.75, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("~:-95.99");
	ok( range != NULL, "~:-95.99' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == -95.99, "End correct (with rounding errors)");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("12345678901234567890:");
	temp = atof("12345678901234567890");		/* Can't just use this because number too large */
	ok( range != NULL, "'12345678901234567890:' is valid range");
	ok( range->start == temp, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end_infinity == TRUE, "Using infinity");
	/* Cannot do a "-1" on temp, as it appears to be same value */
	ok( check_range(temp/1.1, range) == TRUE, "12345678901234567890/1.1 - alert");
	ok( check_range(temp, range) == FALSE, "12345678901234567890 - no alert");
	ok( check_range(temp*2, range) == FALSE, "12345678901234567890*2 - no alert");
	free(range);

	range = parse_range_string("~:0");
	ok( range != NULL, "'~:0' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == 0, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == OUTSIDE, "Will alert on outside of this range");
	ok( check_range(0.5, range) == TRUE, "0.5 - alert");
	ok( check_range(-10, range) == FALSE, "-10 - no alert");
	ok( check_range(0, range)   == FALSE, "0 - no alert");
	free(range);
	
	range = parse_range_string("@0:657.8210567");
	ok( range != 0, "@0:657.8210567' is a valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 657.8210567, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == INSIDE, "Will alert on inside of this range" );
	ok( check_range(32.88, range) == TRUE, "32.88 - alert");
	ok( check_range(-2, range)    == FALSE, "-2 - no alert");
	ok( check_range(657.8210567, range) == TRUE, "657.8210567 - alert");
	ok( check_range(0, range)     == TRUE, "0 - alert");
	free(range);

	range = parse_range_string("@1:1");
	ok( range != NULL, "'@1:1' is a valid range");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 1, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == INSIDE, "Will alert on inside of this range" );
	ok( check_range(0.5, range) == FALSE, "0.5 - no alert");
	ok( check_range(1, range) == TRUE, "1 - alert");
	ok( check_range(5.2, range) == FALSE, "5.2 - no alert");
	free(range);

	range = parse_range_string("1:1");
	ok( range != NULL, "'1:1' is a valid range");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 1, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( check_range(0.5, range) == TRUE, "0.5 - alert");
	ok( check_range(1, range) == FALSE, "1 - no alert");
	ok( check_range(5.2, range) == TRUE, "5.2 - alert");
	free(range);

	range = parse_range_string("2:1");
	ok( range == NULL, "'2:1' rejected");

	rc = _set_thresholds(&thresholds, NULL, NULL);
	ok( rc == 0, "Thresholds (NULL, NULL) set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical == NULL, "Critical not set");

	rc = _set_thresholds(&thresholds, NULL, "80");
	ok( rc == 0, "Thresholds (NULL, '80') set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical->end == 80, "Critical set correctly");

	rc = _set_thresholds(&thresholds, "5:33", NULL);
	ok( rc == 0, "Thresholds ('5:33', NULL) set");
	ok( thresholds->warning->start == 5, "Warning start set");
	ok( thresholds->warning->end == 33, "Warning end set");
	ok( thresholds->critical == NULL, "Critical not set");

	rc = _set_thresholds(&thresholds, "30", "60");
	ok( rc == 0, "Thresholds ('30', '60') set");
	ok( thresholds->warning->end == 30, "Warning set correctly");
	ok( thresholds->critical->end == 60, "Critical set correctly");
	ok( get_status(15.3, thresholds) == STATE_OK, "15.3 - ok");
	ok( get_status(30.0001, thresholds) == STATE_WARNING, "30.0001 - warning");
	ok( get_status(69, thresholds) == STATE_CRITICAL, "69 - critical");

	char *test;
	test = np_escaped_string("bob\\n");
	ok( strcmp(test, "bob\n") == 0, "bob\\n ok");
	free(test);

	test = np_escaped_string("rhuba\\rb");
	ok( strcmp(test, "rhuba\rb") == 0, "rhuba\\rb okay");
	free(test);

	test = np_escaped_string("ba\\nge\\r");
	ok( strcmp(test, "ba\nge\r") == 0, "ba\\nge\\r okay");
	free(test);

	test = np_escaped_string("\\rabbi\\t");
	ok( strcmp(test, "\rabbi\t") == 0, "\\rabbi\\t okay");
	free(test);

	test = np_escaped_string("and\\\\or");
	ok( strcmp(test, "and\\or") == 0, "and\\\\or okay");
	free(test);

	test = np_escaped_string("bo\\gus");
	ok( strcmp(test, "bogus") == 0, "bo\\gus okay");
	free(test);

	test = np_escaped_string("everything");
	ok( strcmp(test, "everything") == 0, "everything okay");

	/* np_extract_ntpvar tests (23) */
	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foo");
	ok(test && !strcmp(test, "bar"), "1st test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar,bar=foo,foobar=barfoo\n", "bar");
	ok(test && !strcmp(test, "foo"), "2nd test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foobar");
	ok(test && !strcmp(test, "barfoo"), "3rd test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar\n", "foo");
	ok(test && !strcmp(test, "bar"), "Single test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfooi\n", "abcd");
	ok(!test, "Key not found 1");

	test=np_extract_ntpvar("foo=bar\n", "abcd");
	ok(!test, "Key not found 2");

	test=np_extract_ntpvar("foo=bar=foobar", "foo");
	ok(test && !strcmp(test, "bar=foobar"), "Strange string 1");
	free(test);

	test=np_extract_ntpvar("foo", "foo");
	ok(!test, "Malformed string 1");

	test=np_extract_ntpvar("foo,", "foo");
	ok(!test, "Malformed string 2");

	test=np_extract_ntpvar("foo=", "foo");
	ok(!test, "Malformed string 3");

	test=np_extract_ntpvar("foo=,bar=foo", "foo");
	ok(!test, "Malformed string 4");

	test=np_extract_ntpvar(",foo", "foo");
	ok(!test, "Malformed string 5");

	test=np_extract_ntpvar("=foo", "foo");
	ok(!test, "Malformed string 6");

	test=np_extract_ntpvar("=foo,", "foo");
	ok(!test, "Malformed string 7");

	test=np_extract_ntpvar(",,,", "foo");
	ok(!test, "Malformed string 8");

	test=np_extract_ntpvar("===", "foo");
	ok(!test, "Malformed string 9");

	test=np_extract_ntpvar(",=,=,", "foo");
	ok(!test, "Malformed string 10");

	test=np_extract_ntpvar("=,=,=", "foo");
	ok(!test, "Malformed string 11");

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foo");
	ok(test && !strcmp(test, "bar"), "Random spaces and newlines 1");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 2");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foobar");
	ok(test && !strcmp(test, "barfoo"), "Random spaces and newlines 3");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar\n \n= \n foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 4");
	free(test);

	test=np_extract_ntpvar("", "foo");
	ok(!test, "Empty string return NULL");


	/* This is the result of running ./test_utils */
	temp_string = (char *) _np_state_generate_key();
	ok(!strcmp(temp_string, "83d877b6cdfefb5d6f06101fd6fe76762f21792c"), "Got hash with exe and no parameters" ) || 
        diag( "You are probably running in wrong directory. Must run as ./test_utils" );


	this_nagios_plugin->argc=4;
	this_nagios_plugin->argv[0] = "./test_utils";
	this_nagios_plugin->argv[1] = "here";
	this_nagios_plugin->argv[2] = "--and";
	this_nagios_plugin->argv[3] = "now";
	temp_string = (char *) _np_state_generate_key();
	ok(!strcmp(temp_string, "94b5e17bf5abf51cb15aff5f69b96f2f8dac5ecd"), "Got based on expected argv" );

	unsetenv("NAGIOS_PLUGIN_STATE_DIRECTORY");
	temp_string = (char *) _np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory" );

	setenv("NAGIOS_PLUGIN_STATE_DIRECTORY", "", 1);
	temp_string = (char *) _np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, NP_STATE_DIR_PREFIX), "Got default directory even with empty string" );

	setenv("NAGIOS_PLUGIN_STATE_DIRECTORY", "/usr/local/nagios/var", 1);
	temp_string = (char *) _np_state_calculate_location_prefix();
	ok(!strcmp(temp_string, "/usr/local/nagios/var"), "Got default directory" );



	ok(temp_state_key==NULL, "temp_state_key initially empty");

	this_nagios_plugin->argc=1;
	this_nagios_plugin->argv[0] = "./test_utils";
	np_enable_state(NULL, 51);
	temp_state_key = this_nagios_plugin->state;
	ok( !strcmp(temp_state_key->plugin_name, "check_test"), "Got plugin name" );
	ok( !strcmp(temp_state_key->name, "83d877b6cdfefb5d6f06101fd6fe76762f21792c"), "Got generated filename" );


	np_enable_state("allowedchars_in_keyname", 77);
	temp_state_key = this_nagios_plugin->state;
	ok( !strcmp(temp_state_key->plugin_name, "check_test"), "Got plugin name" );
	ok( !strcmp(temp_state_key->name, "allowedchars_in_keyname"), "Got key name with valid chars" );
	ok( !strcmp(temp_state_key->_filename, "/usr/local/nagios/var/check_test/allowedchars_in_keyname"), "Got internal filename" );


	/* Don't do this test just yet. Will die */
	/*
	np_enable_state("bad^chars$in@here", 77);
	temp_state_key = this_nagios_plugin->state;
	ok( !strcmp(temp_state_key->name, "bad_chars_in_here"), "Got key name with bad chars replaced" );
	*/

	np_enable_state("funnykeyname", 54);
	temp_state_key = this_nagios_plugin->state;
	ok( !strcmp(temp_state_key->plugin_name, "check_test"), "Got plugin name" );
	ok( !strcmp(temp_state_key->name, "funnykeyname"), "Got key name" );



	ok( !strcmp(temp_state_key->_filename, "/usr/local/nagios/var/check_test/funnykeyname"), "Got internal filename" );
	ok( temp_state_key->data_version==54, "Version set" );

	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Got no state data as file does not exist" );


/*
	temp_fp = fopen("var/statefile", "r");
	if (temp_fp==NULL) 
		printf("Error opening. errno=%d\n", errno);
	printf("temp_fp=%s\n", temp_fp);
	ok( _np_state_read_file(temp_fp) == TRUE, "Can read state file" );
	fclose(temp_fp);
*/
	
	temp_state_key->_filename="var/statefile";
	temp_state_data = np_state_read();
	ok( this_nagios_plugin->state->state_data!=NULL, "Got state data now" ) || diag("Are you running in right directory? Will get coredump next if not");
	ok( this_nagios_plugin->state->state_data->time==1234567890, "Got time" );
	ok( !strcmp((char *)this_nagios_plugin->state->state_data->data, "String to read"), "Data as expected" );

	temp_state_key->data_version=53;
	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Older data version gives NULL" );
	temp_state_key->data_version=54;

	temp_state_key->_filename="var/nonexistant";
	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Missing file gives NULL" );
	ok( this_nagios_plugin->state->state_data==NULL, "No state information" );

	temp_state_key->_filename="var/oldformat";
	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Old file format gives NULL" );

	temp_state_key->_filename="var/baddate";
	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Bad date gives NULL" );

	temp_state_key->_filename="var/missingdataline";
	temp_state_data = np_state_read();
	ok( temp_state_data==NULL, "Missing data line gives NULL" );




	unlink("var/generated");
	temp_state_key->_filename="var/generated";
	current_time=1234567890;
	np_state_write_string(current_time, "String to read");
	ok(system("cmp var/generated var/statefile")==0, "Generated file same as expected");




	unlink("var/generated_directory/statefile");
	unlink("var/generated_directory");
	temp_state_key->_filename="var/generated_directory/statefile";
	current_time=1234567890;
	np_state_write_string(current_time, "String to read");
	ok(system("cmp var/generated_directory/statefile var/statefile")==0, "Have created directory");

	/* This test to check cannot write to dir - can't automate yet */
	/*
	unlink("var/generated_bad_dir");
	mkdir("var/generated_bad_dir", S_IRUSR);
	np_state_write_string(current_time, "String to read");
	*/


	temp_state_key->_filename="var/generated";
	time(&current_time);
	np_state_write_string(0, "String to read");
	temp_state_data = np_state_read();
	/* Check time is set to current_time */
	ok(system("cmp var/generated var/statefile > /dev/null")!=0, "Generated file should be different this time");
	ok(this_nagios_plugin->state->state_data->time-current_time<=1, "Has time generated from current time");
	

	/* Don't know how to automatically test this. Need to be able to redefine die and catch the error */
	/*
	temp_state_key->_filename="/dev/do/not/expect/to/be/able/to/write";
	np_state_write_string(0, "Bad file");
	*/
	

	np_cleanup();

	ok( this_nagios_plugin==NULL, "Free'd this_nagios_plugin" );

	return exit_status();
}

