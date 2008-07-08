#ifndef _UTILS_CMD_
#define _UTILS_CMD_

/* 
 * Header file for nagios plugins utils_cmd.c
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

#endif /* _UTILS_CMD_ */
