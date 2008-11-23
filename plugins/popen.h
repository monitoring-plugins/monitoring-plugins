/******************************************************************************
*
*
*****************************************************************************/

FILE *spopen (const char *);
int spclose (FILE *);
RETSIGTYPE popen_timeout_alarm_handler (int);

extern unsigned int timeout_interval;
pid_t *childpid=NULL;
int *child_stderr_array=NULL;
FILE *child_process=NULL;
FILE *child_stderr=NULL;
