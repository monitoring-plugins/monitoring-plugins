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
* This executable works by reading process address structures, so needs
* to be executed as root
*
* Originally written by R.W.Ingraham
* Rewritten by Duncan Ferguson (Altinity Ltd, June 2008)
*   The rewrite was necessary as /dev/kmem is not available within
*   non-global zones on Solaris 10
*
*   Details for rewrite came from
*    source of /usr/ucb/ps on Solaris:
*     http://cvs.opensolaris.org/source/xref/onnv/onnv-gate/usr/src/ucbcmd/ps/ps.c#argvoff
*    usenet group posting
*     http://groups.google.com/group/comp.unix.solaris/tree/browse_frm/month/2001-09/bfa40c08bac819a2?rnum=141&_done=%2Fgroup%2Fcomp.unix.solaris%2Fbrowse_frm%2Fmonth%2F2001-09%3F
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <procfs.h>
#include <sys/types32.h>

/*
 *  Constants
 */

#define PROC_DIR  "/proc"
#define ARGS            30

/*
 *  Globals
 */

static char *        szProg;

/*
 *  Prototypes
 */
void usage();

/*----------------------------------------------------------------------------*/

int main (int argc, char **argv)
{
  DIR *procdir;
  struct dirent *proc;
  char ps_name[ARGS];
  char as_name[ARGS];
  psinfo_t psinfo;

  /* Set our program name global */
  if ((szProg = strrchr(argv[0], '/')) != NULL)
    szProg++;
  else
    szProg = argv[0];

  /* if given any parameters, print out help */
  if(argc > 1) {
    (void)usage();
    exit(1);
  }

  /* Make sure that our euid is root */
  if (geteuid() != 0)
  {
    fprintf(stderr, "%s: This program can only be run by the root user!\n", szProg);
    exit(1);
  }

  if ((procdir = opendir(PROC_DIR)) == NULL) {
    fprintf(stderr, "%s: cannot open PROC directory %s\n", szProg, PROC_DIR);
    exit(1);
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
  while((proc = readdir(procdir))) {
    int ps_fd;
    int as_fd;
    off_t argoff;
    int i;
    char *args;
    char *procname;
    char *ptr;
    int argslen;
    uintptr_t args_addr;;
    uintptr_t *args_vecs;;
    int args_count;

    if(proc->d_name[0] == '.')
      continue;

    sprintf(ps_name,"%s/%s/%s",PROC_DIR,proc->d_name,"psinfo");
    sprintf(as_name,"%s/%s/%s",PROC_DIR,proc->d_name,"as");
try_again:
    if((ps_fd = open(ps_name, O_RDONLY)) == -1)
      continue;

    if((as_fd = open(as_name, O_RDONLY)) == -1)
      continue;

    if(read(ps_fd, &psinfo, sizeof(psinfo)) != sizeof(psinfo)) {
      int err = errno;
      close(ps_fd);
      close(as_fd);
      if(err == EAGAIN) goto try_again;
      if(err != ENOENT)
        fprintf(stderr, "%s: read() on %s: %s\n", szProg,
          ps_name, strerror(err));
      continue;
    }
    close(ps_fd);

    /* system process, ignore since the previous version did */
    if(
      psinfo.pr_nlwp == 0 ||
      strcmp(psinfo.pr_lwp.pr_clname, "SYS") == 0
    ) {
      continue;
    }

    /* get the procname to match previous versions */
    procname = strdup(psinfo.pr_psargs);
    if((ptr = strchr(procname, ' ')) != NULL)
        *ptr = '\0';
    if((ptr = strrchr(procname, '/')) != NULL)
        ptr++;
    else
        ptr = procname;

    /*
     * print out what we currently know
     */
    printf("%c %5d %5d %5d %6lu %6lu %4.1f %s ",
      psinfo.pr_lwp.pr_sname,
      psinfo.pr_euid,
      psinfo.pr_pid,
      psinfo.pr_ppid,
      psinfo.pr_size,
      psinfo.pr_rssize,
      ((float)(psinfo.pr_pctcpu) / 0x8000 * 100.0),
      ptr
    );
    free(procname);

    /*
     * and now for the command line stuff
     */

    args_addr = psinfo.pr_argv;
    args_count = psinfo.pr_argc;
    args_vecs = malloc(args_count * sizeof(uintptr_t));

    if(psinfo.pr_dmodel == PR_MODEL_NATIVE) {
      /* this process matches target process */
      pread(as_fd,args_vecs, args_count * sizeof(uintptr_t),
        args_addr);
    } else {
      /* this process is 64bit, target process is 32 bit*/
      caddr32_t *args_vecs32 = (caddr32_t *)args_vecs;
      pread(as_fd,args_vecs32,args_count * sizeof(caddr32_t),
        args_addr);
      for (i=args_count-1;i>=0;--i)
        args_vecs[i]=args_vecs32[i];
    }

    /*
     * now read in the args - if what we read in fills buffer
     * resize buffer and reread that bit again
     */
    argslen=ARGS;
    args=malloc(argslen+1);
    for(i=0;i<args_count;i++) {
      memset(args,'\0',argslen+1);
      if(pread(as_fd, args, argslen, args_vecs[i]) <= 0) {
        break;
      }
      args[argslen]='\0';
      if(strlen(args) == argslen){
        argslen += ARGS;
        args = realloc(args, argslen + 1);
        i--;
        continue;
      }
      printf(" %s", args);
    }
    free(args_vecs);
    free(args);
    close(as_fd);
    printf("\n");
  }

  (void) closedir(procdir);

  return (0);
}

/*----------------------------------------------------------------------------*/

void usage() {
  printf("%s: Help output\n\n", szProg);
  printf("If this program is given any arguments, this help is displayed.\n");
  printf("This command is used to print out the full command line for all\n");
  printf("running processes because /usr/bin/ps is limited to 80 chars and\n");
  printf("/usr/ucb/ps can merge columns together.\n\n");
  printf("Columns are:\n");
  printf("\tS        - State of process - see 'ps' man page\n");
  printf("\tUID      - UID of the process owner\n");
  printf("\tPID      - PID of the process\n");
  printf("\tPPID     - PID of the parent process\n");
  printf("\tVSZ      - Virtual memory usage (kilobytes)\n");
  printf("\tRSS      - Real memory usage (kilobytes)\n");
  printf("\t%%CPU     - CPU usage\n");
  printf("\tCOMMAND  - Command being run\n");
  printf("\tARGS     - Full command line with arguements\n");
  return;
}
