/* Header file for utils_tcp */

int np_expect_match(char* status, char** server_expect, int server_expect_count,
                    int all, int exact_match, int verbose);
