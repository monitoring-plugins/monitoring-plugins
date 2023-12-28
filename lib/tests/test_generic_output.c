
#include "../lib/output.h"
#include "../../tap/tap.h"

void test_one_subcheck();
void test_two_subchecks();

void test_perfdata_formatting();
void test_perfdata_formatting2();

void test_deep_check_hierarchy();
void test_deep_check_hierarchy2();

int main(void) {

	plan_tests(10);

	diag("Simple test with one subcheck");
	test_one_subcheck();

	diag("Test with two subchecks");
	test_two_subchecks();

	diag("Test for performance data formatting");
	test_perfdata_formatting();

	diag("Another test for performance data formatting");
	test_perfdata_formatting2();

	diag("Test for deeper hierarchies");
	test_deep_check_hierarchy();

	diag("Another test for deeper hierarchies");
	test_deep_check_hierarchy2();

	return exit_status();
}

void test_one_subcheck() {
	mp_subcheck sc1 = mp_subcheck_init();

	sc1.output = "foobar";
	sc1.state = STATE_WARNING;

	mp_check check = mp_check_init();
	mp_add_subcheck_to_check(&check, sc1);

	char *output = mp_fmt_output(check);

	// diag("Formatted output");
	// diag(output);

	char expected[] = "[WARNING] - ok=0, warning=1, critical=0, unknown=0\n"
		"\t\\_[WARNING] - foobar\n";

	//diag("Expected output");
	//diag(expected);

	ok(strcmp(output, expected) == 0, "Simple output test");

	return;
}

void test_perfdata_formatting2() {
	mp_perfdata pd1 = perfdata_init();
	mp_perfdata pd2 = perfdata_init();

	pd1.label = "foo";
	pd2.label = "bar";

	pd1 = mp_set_pd_value(pd1, 23);
	pd2 = mp_set_pd_value(pd2, 1LL);

	pd_list *tmp = pd_list_init();

	pd_list_append(tmp, pd1);
	pd_list_append(tmp, pd2);

	char *result = pd_list_to_string(*tmp);

	ok(strcmp(result, "foo=23;;; bar=1;;;") == 0, "Perfdata string formatting");

	return;
}

void test_perfdata_formatting() {
	mp_perfdata pd1 = perfdata_init();

	pd1.uom = "s";
	pd1.label = "foo";

	pd1 = mp_set_pd_value(pd1, 23);

	char *pd_string = pd_to_string(pd1);

	ok(strcmp(pd_string, "foo=23s;;;") == 0, "Perfdata string formatting");

	return;
}

void test_two_subchecks() {
	mp_subcheck sc1 = mp_subcheck_init();

	sc1.output = "foobar";
	sc1.state = STATE_WARNING;

	mp_perfdata pd1 = perfdata_init();

	pd1 = mp_set_pd_value(pd1, 23);

	pd1.uom = "s";
	pd1.label = "foo";


	mp_add_perfdata_to_subcheck(&sc1, pd1);

	mp_subcheck sc2 = mp_subcheck_init();
	sc2.output = "baz";
	sc2.state = STATE_OK;

	mp_add_subcheck_to_subcheck(&sc1, sc2);

	mp_check check = mp_check_init();
	mp_add_subcheck_to_check(&check, sc1);

	char *output = mp_fmt_output(check);

	//diag("Formatted output. Length: %u", strlen(output));
	//diag(output);

	ok(output != NULL, "Output should not be NULL");

	char expected[] = "[WARNING] - ok=0, warning=1, critical=0, unknown=0\n"
		"\t\\_[WARNING] - foobar\n"
		"\t\t\\_[OK] - baz\n"
		"|foo=23s;;; \n";

	//diag("Expected output. Length: %u", strlen(expected));
	//diag(expected);

	ok(strcmp(output, expected) == 0, "Output is as expected");
}

void test_deep_check_hierarchy() {
	// level 4
	mp_subcheck sc4 = mp_subcheck_init();
	sc4.output = "level4";
	sc4.state = STATE_OK;


	// level 3
	mp_subcheck sc3 = mp_subcheck_init();
	sc3.output = "level3";
	sc3.state = STATE_OK;


	// level 2
	mp_subcheck sc2 = mp_subcheck_init();
	sc2.output = "baz";
	sc2.state = STATE_OK;


	// level 1
	mp_subcheck sc1 = mp_subcheck_init();

	sc1.output = "foobar";
	sc1.state = STATE_WARNING;

	mp_perfdata pd1 = perfdata_init();

	pd1.uom = "s";
	pd1.label = "foo";
	pd1 = mp_set_pd_value(pd1, 23);

	mp_add_perfdata_to_subcheck(&sc1, pd1);

	// main check
	mp_check check = mp_check_init();

	mp_add_subcheck_to_subcheck(&sc3, sc4);
	mp_add_subcheck_to_subcheck(&sc2, sc3);
	mp_add_subcheck_to_subcheck(&sc1, sc2);
	mp_add_subcheck_to_check(&check, sc1);

	char *output = mp_fmt_output(check);

	int output_length = strlen(output);

	//diag("Formatted output of length %i", output_length);
	//diag(output);

	ok(output != NULL, "Output should not be NULL");

	char expected[] = "[WARNING] - ok=0, warning=1, critical=0, unknown=0\n"
		"\t\\_[WARNING] - foobar\n"
		"\t\t\\_[OK] - baz\n"
		"\t\t\t\\_[OK] - level3\n"
		"\t\t\t\t\\_[OK] - level4\n"
		"|foo=23s;;; \n";

	int expected_length = strlen(expected);

	//diag("Expected output of length: %i", expected_length);
	//diag(expected);

	ok(output_length == expected_length, "Outputs are of equal length");
	ok(strcmp(output, expected) == 0, "Output is as expected");
}

void test_deep_check_hierarchy2() {
	// level 1
	mp_subcheck sc1 = mp_subcheck_init();

	sc1.output = "foobar";
	sc1.state = STATE_WARNING;

	mp_perfdata pd1 = perfdata_init();
	pd1.uom = "s";
	pd1.label = "foo";
	pd1 = mp_set_pd_value(pd1, 23);

	mp_add_perfdata_to_subcheck(&sc1, pd1);

	// level 2
	mp_subcheck sc2 = mp_subcheck_init();
	sc2.output = "baz";
	sc2.state = STATE_OK;

	mp_perfdata pd2 = perfdata_init();
	pd2.uom = "B";
	pd2.label = "baz";
	pd2 = mp_set_pd_value(pd2, 1024);
	mp_add_perfdata_to_subcheck(&sc2, pd2);


	// level 3
	mp_subcheck sc3 = mp_subcheck_init();
	sc3.output = "level3";
	sc3.state = STATE_OK;

	mp_perfdata pd3 = perfdata_init();
	pd3.label = "floatMe";
	pd3 = mp_set_pd_value(pd3, 1024.1024);
	mp_add_perfdata_to_subcheck(&sc3, pd3);


	// level 4
	mp_subcheck sc4 = mp_subcheck_init();
	sc4.output = "level4";
	sc4.state = STATE_OK;

	mp_check check = mp_check_init();

	mp_add_subcheck_to_subcheck(&sc3, sc4);
	mp_add_subcheck_to_subcheck(&sc2, sc3);
	mp_add_subcheck_to_subcheck(&sc1, sc2);
	mp_add_subcheck_to_check(&check, sc1);

	char *output = mp_fmt_output(check);

	// diag("Formatted output of length: %i", strlen(output));
	// diag(output);

	ok(output != NULL, "Output should not be NULL");

	char expected[] = "[WARNING] - ok=0, warning=1, critical=0, unknown=0\n"
		"\t\\_[WARNING] - foobar\n"
		"\t\t\\_[OK] - baz\n"
		"\t\t\t\\_[OK] - level3\n"
		"\t\t\t\t\\_[OK] - level4\n"
		"|foo=23s;;; baz=1024B;;; floatMe=1024.102400;;; \n";

	// diag("Expected output of length: %i", strlen(expected));
	// diag(expected);

	ok(strcmp(output, expected) == 0, "Output is as expected");
}
