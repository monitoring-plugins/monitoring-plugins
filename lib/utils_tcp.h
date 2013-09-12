/* Header file for utils_tcp */

#define NP_MATCH_ALL            0x1
#define NP_MATCH_EXACT          0x2
#define NP_MATCH_VERBOSE        0x4

int np_expect_match(char* status, char** server_expect, int server_expect_count,
                    int flags);
