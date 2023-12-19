
#include "../check_swap.d/check_swap.h"
#include "../../tap/tap.h"

void print_usage() {};
void print_help(swap_config config) {
	(void) config;
};

const char *progname = "test_check_swap";

int main() {

	swap_config config = swap_config_init();

	swap_result test_data = get_swap_data(config);

	plan_tests(1);

	ok(test_data.errorcode == 0, "Test whether we manage to retrieve swap data");
}
