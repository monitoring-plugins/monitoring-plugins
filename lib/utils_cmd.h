#ifndef _UTILS_CMD_
#define _UTILS_CMD_

/* 
 * Header file for Monitoring Plugins utils_cmd.c
 * 
 * 
 */

/** types **/
struct output
{
	char *buf;     /* output buffer */
	size_t buflen; /* output buffer content length */
	char **line;   /* array of lines (points to buf) */
	size_t *lens;  /* string lengths */
	size_t lines;  /* lines of output */
};

typedef struct output output;

/** prototypes **/
int cmd_run (const char *, output *, output *, int);
int cmd_run_array (char *const *, output *, output *, int);
int cmd_file_read (char *, output *, int);

/* only multi-threaded plugins need to bother with this */
void cmd_init (void);
#define CMD_INIT cmd_init()

/* possible flags for cmd_run()'s fourth argument */
#define CMD_NO_ARRAYS 0x01   /* don't populate arrays at all */
#define CMD_NO_ASSOC 0x02    /* output.line won't point to buf */

/* This variable must be global, since there's no way the caller
 * can forcibly slay a dead or ungainly running program otherwise.
 * Multithreading apps and plugins can initialize it (via CMD_INIT)
 * in an async safe manner PRIOR to calling cmd_run() or cmd_run_array()
 * for the first time.
 *
 * The check for initialized values is atomic and can
 * occur in any number of threads simultaneously. */
static pid_t *_cmd_pids = NULL;

RETSIGTYPE timeout_alarm_handler (int);


#endif /* _UTILS_CMD_ */
