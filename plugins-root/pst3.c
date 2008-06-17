/*****************************************************************************
* 
* pst3
* 
* License: GPL
* Copyright (c) 2008 Nagios Plugin Development Team
* 
* Description:
* 
* This file contains the pst3 executable. This is a replacement ps command
* for Solaris to get output which provides a long argument listing, which 
* is not possible with the standard ps command (due to truncation). /usr/ucb/ps
* also has issues where some fields run into each other.
* 
* This executable works by reading the kernel memory structures, so needs
* to be executed as root
*
* Originally written by R.W.Ingraham
* 
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
*****************************************************************************/

#define _KMEMUSER	1

#include <kvm.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <procfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>


/*
 *	Constants
 */

#define PROC_DIR	"/proc"
#define MAX_PATH	1024
#define OK 0
#define FAIL NULL


/*
 *	Structures
 */


/*
 *	Globals
 */

static char *        szProg;
static kvm_t *       kd;
static struct proc * pProc;
static struct user * pUser;
static char **       myArgv;


/*
 *	Prototypes
 */

static void output_info(struct proc *proc_kvm,char **proc_argv);
static void HandleProc(struct proc *proc);

/*----------------------------------------------------------------------------*/

int main (int argc, char **argv)
{
	DIR *pDir;
	struct dirent *pDent;
	int retcode = 0;
	struct proc *proc;
	struct pid pid;

	/* Set our program name global */
	if ((szProg = strrchr(argv[0], '/')) != NULL)
		szProg++;
	else
		szProg = argv[0];

	/* Make sure that our euid is root */
	if (geteuid() != 0)
	{
		fprintf(stderr, "%s: This program can only be run by the root user!\n", szProg);
		exit(1);
	}

	/* Get a handle to the running kernel image */
	if ((kd = kvm_open(NULL, NULL, NULL, O_RDONLY, argv[0])) == NULL)
	{
		fprintf(stderr, "%s: Failed to open kernel memory: %s\n", szProg, strerror(errno));
		exit(2);
	}

	/* reset to first proc in list */
	if(kvm_setproc(kd) == -1) {
                perror("kvm_setproc");
                exit(2);
        }

	/* Display column headings */
	printf("%c %5s %5s %5s %6s %6s %4s %s %s\n",
                'S',
                "UID",
                "PID",
                "PPID",
                "VSZ",
                "RSS",
                "%CPU",
                "COMMAND",
                "ARGS"
        );

	/* Zip through all of the process entries */
	while((proc = kvm_nextproc(kd)) != 0) {
		HandleProc(proc);
	}

	/* Close the handle to the running kernel image */
	kvm_close(kd);

	return retcode;
}

/*----------------------------------------------------------------------------*/

static void HandleProc(struct proc *proc)
{
	struct pid pid;
	struct user *user;
	char **proc_argv = NULL;

	if(kvm_kread(kd, (unsigned long) proc->p_pidp, (char *) &pid, sizeof pid) == -1) {
		perror("kvm_read error");
		exit(2);
	}
	proc->p_pidp = &pid;
	user = kvm_getu(kd, proc);

	if(kvm_getcmd(kd, proc, user, &proc_argv, NULL) == -1) {
		return;
	}

	if(proc_argv == NULL) {
		return;
	}

	output_info(proc, proc_argv);
	free(proc_argv);
}

static void output_info(struct proc *proc_kvm, char **proc_argv)
{
	char procpath[MAX_PATH];
	psinfo_t procinfo;
	int fd, len;
	char *procname;
	int i;

	sprintf(procpath, "/proc/%d/psinfo", proc_kvm->p_pidp->pid_id);

	if ((fd = open(procpath, O_RDONLY)) >= 0)
	{
		if ((len = read(fd, &procinfo, sizeof(procinfo))) != sizeof(procinfo))
		{
			fprintf(stderr,"%s: Read error of psinfo structure (%d)\n", procpath, len);
			exit(2);
		}
		close(fd);
	}

	if((procname = strrchr(proc_argv[0], '/')) != NULL)
		procname++;
	else
		procname = proc_argv[0];

	printf("%c %5d %5d %5d %6lu %6lu %4.1f %s ",
		procinfo.pr_lwp.pr_sname,
		(int)(procinfo.pr_euid),
		(int)proc_kvm->p_pidp->pid_id,
		(int)proc_kvm->p_ppid,
		(unsigned long)(procinfo.pr_size),
		(unsigned long)(procinfo.pr_rssize),
		((float)(procinfo.pr_pctcpu) / 0x8000 * 100.0),
		procname
	);

	for(i=0;proc_argv[i];i++) {
		printf(" %s", proc_argv[i]);
	}

	printf("\n");
}

