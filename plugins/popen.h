#pragma once
#include "../config.h"
#include <stdio.h>

FILE *mopl_popen_spopen(const char *);
int mopl_popen_spclose(FILE *);
void mopl_popen_popen_timeout_alarm_handler(int);

pid_t *childpid = NULL;
int *child_stderr_array = NULL;
FILE *child_process = NULL;
FILE *child_stderr = NULL;
