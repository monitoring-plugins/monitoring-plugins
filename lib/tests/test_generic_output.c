
#include "../lib/output.h"
#include "../../tap/tap.h"

int main(void) {
	mp_subcheck sc1 = mp_subcheck_init();

	sc1.output = "foobar";
	sc1.state = STATE_WARNING;

	mp_perfdata pd1 = perfdata_init();

	pd1.uom = "s";
	pd1.label = "foo";

	mp_perfdata_value pd_val1 = { 0 };

	pd_val1.pd_int = 23;
	pd_val1.type = PD_TYPE_INT;

	pd1.value = pd_val1;

	char *pd_string = pd_to_string(pd1);

	pd_list_append(sc1.perfdata, pd1);

	mp_check check = mp_check_init();
	mp_add_subcheck(&check, sc1);

	char *output = mp_fmt_output(check);

	//diag("Formatted output");
	//diag(output);

	plan_tests(3);

	ok(strcmp(pd_string, "foo=23s;;;") == 0, "Perfdata string formatting");

	ok(output != NULL, "Output should not be NULL");

	char expected[] = "[OK] - ok=0, warning=1, critical=0, unknown=0\n"
		"\t\\_[WARNING] - foobar\n"
		"|foo=23s;;;\n";

	//diag("Expected output");
	//diag(expected);

	ok(strcmp(output, expected) == 0, "Output is as expected");

	return exit_status();
}
