#pragma once

typedef struct get_num_of_users_wrapper {
	int errorcode;
	int users;
} get_num_of_users_wrapper;

enum {
	NO_SYSTEMD_ERROR = 64,
	WINDOWS_COULD_NOT_ENUMERATE_SESSIONS,
	COULD_NOT_OPEN_PIPE,
	STDERR_COULD_NOT_BE_READ,
};

get_num_of_users_wrapper get_num_of_users_systemd();
get_num_of_users_wrapper get_num_of_users_utmp();
get_num_of_users_wrapper get_num_of_users_windows();
get_num_of_users_wrapper get_num_of_users_who_command();
