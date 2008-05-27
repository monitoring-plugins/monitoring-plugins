/*	pst3.c
*
*  Third version to get process arg info; this time by using
*  a combination of reading the /proc/<pid>/psinfo structures
*  and reading the complete arg vector from kernel memory structures.
*
*  Developed and tested under Solaris 5.8 (both 32 and 64 bit modes).
*
*  NOTE:  This program must be setuid-root (or run by root) to work!
*
*	Written: 2005-04-28	R.W.Ingraham
*/


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

static int HandleFile (struct dirent *pDent);
static int HandlePsInfo (char *szPath, psinfo_t *pPsInfo);
static int GetArgVectors (pid_t pid);
static void ShowArgVectors (void);
static void ReleaseArgVectors();


/*----------------------------------------------------------------------------*/

int main (int argc, char **argv)
{
	DIR *pDir;
	struct dirent *pDent;
	int retcode = 0;


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

	/* Open the /proc directory */
	if ((pDir = opendir(PROC_DIR)) != NULL)
	{
		/* Display column headings */
		printf("S   UID   PID  PPID    VSZ    RSS %%CPU COMMAND ARGS\n");

		/* Zip through all of the process entries */
		while ((pDent = readdir(pDir)) != NULL)
		{
			/* Handle each pid sub-directory */
			HandleFile(pDent);
		}

		/* Close the directory */
		closedir(pDir);
	}
	else	/* ERROR: Failure to open PROC_DIR */
	{
		fprintf(stderr, "%s: Failed to open \"%s\": %s\n", szProg, PROC_DIR, strerror(errno));
		retcode = 3;
	}

	/* Close the handle to the running kernel image */
	kvm_close(kd);

	return retcode;
}

/*----------------------------------------------------------------------------*/

static int HandleFile (struct dirent *pDent)
{
	char szPath[MAX_PATH];
	psinfo_t sPsInfo;
	int fd, len;
	int rc = 0;

	/* Skip files beginning with a "." */
	if (pDent->d_name[0] == '.')
		return 0;

	/* Cosntruct the path to the psinfo file */
	len = sprintf(szPath, "%s/%s/psinfo", PROC_DIR, pDent->d_name);

	/* Open the psinfo file for this pid and print out its arg vectors */
	if ((fd = open(szPath, O_RDONLY)) >= 0)
	{
		/* Read the psinfo struct */
		if ((len = read(fd, &sPsInfo, sizeof(sPsInfo))) != sizeof(sPsInfo))
		{
			rc = errno;
			fprintf(stderr, "%s: Read error of psinfo structure (%d)\n", szPath, len);
			return rc;
		}

		/* Close the psinfo file */
		close(fd);

		/* Pass psinfo struct to reporting function */
		HandlePsInfo(szPath, &sPsInfo);
	}
	else if (errno != ENOENT)
	{
		rc = errno;
		fprintf(stderr, "%s: %s\n", szPath, strerror(errno));
	}

	return 0;
}

/*----------------------------------------------------------------------------*/

static int HandlePsInfo (char *szPath, psinfo_t *pPsInfo)
{
	int retcode;
	char *thisProg;

	/* Make sure that the process is still there */
	if ((retcode = GetArgVectors(pPsInfo->pr_pid)) == 0)
	{
		/* We use the program name from the kvm argv[0] instead
		 * of pr_fname from the psinfo struct because pr_fname
		 * may be truncated.
		 *
		 * Also, strip-off leading path information.
		 */
		if ((thisProg = strrchr(myArgv[0], '/')) != NULL)
			thisProg++;
		else
			thisProg = myArgv[0];
 
		/* Display the ps columns (except for argv) */
		printf("%c %5d %5d %5d %6lu %6lu %4.1f %s ",
			pPsInfo->pr_lwp.pr_sname,
			(int)(pPsInfo->pr_euid),
			(int)(pPsInfo->pr_pid),
			(int)(pPsInfo->pr_ppid),
			(unsigned long)(pPsInfo->pr_size),
			(unsigned long)(pPsInfo->pr_rssize),
			((float)(pPsInfo->pr_pctcpu) / 0x8000 * 100.0),
			thisProg);

		/* Display the arg vectors associated with this pid */
		ShowArgVectors();

		/* Release the arg vector buffer memory */
		ReleaseArgVectors();
	}

	return retcode;
}

/*----------------------------------------------------------------------------*/

static int GetArgVectors (pid_t pid)
{
	int retcode = 1;

	/* Get the proc structure for the specified PID */
	if ((pProc = kvm_getproc(kd, pid)) != NULL)
	{
		/* Save a copy of the process' u-area */
		if ((pUser = kvm_getu(kd, pProc)) != NULL)
		{
			/* Reconstruct the process' argv vector array */
			if (kvm_getcmd(kd, pProc, pUser, &myArgv, NULL) == 0)
			{
				retcode = 0;
			}
		}
	}

	return retcode;
}

/*----------------------------------------------------------------------------*/

static void ShowArgVectors (void)
{
	int i;

	for (i=0; myArgv[i]; i++)
	{
		printf(" %s", myArgv[i]);
	}
	printf("\n");
}

/*----------------------------------------------------------------------------*/

static void ReleaseArgVectors()
{
	/* NOOP */
}

/*----------------------------------------------------------------------------*/
