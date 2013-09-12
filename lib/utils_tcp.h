/* Header file for utils_tcp */

#define NP_MATCH_ALL            0x1
#define NP_MATCH_EXACT          0x2
#define NP_MATCH_VERBOSE        0x4

/*
 * The NP_MATCH_RETRY state indicates that matching might succeed if
 * np_expect_match() is called with a longer input string.  This allows the
 * caller to decide whether it makes sense to wait for additional data from the
 * server.
 */
enum np_match_result {
	NP_MATCH_FAILURE,
	NP_MATCH_SUCCESS,
	NP_MATCH_RETRY
};

enum np_match_result np_expect_match(char *status,
                                     char **server_expect,
                                     int server_expect_count,
                                     int flags);
