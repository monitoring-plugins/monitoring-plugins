
#include "../check_swap.d/check_swap.h"
#include "../../tap/tap.h"

void print_usage() {};
void print_help(swap_config config) {
	(void) config;
};

const char *progname = "test_check_swap";

int main() {

	swap_config config = swap_config_init();

	swap_result test_data = getSwapFromProcMeminfo(config, "./var/proc_meminfo");

	plan_tests(4);

	ok(test_data.errorcode == 0, "Test whether we manage to retrieve swap data");
	ok(test_data.metrics.total == 34233905152, "Is the total Swap correct");
	ok(test_data.metrics.free == 34231021568, "Is the free Swap correct");
	ok(test_data.metrics.used == 0, "Is the used Swap correct");
}
