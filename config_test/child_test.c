/* Base code taken from http://www-h.eng.cam.ac.uk/help/tpl/unix/fork.html
 * Fix for redhat suggested by Ptere Pramberger, peter@pramberger.at */
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
void popen_sigchld_handler (int);
int childtermd;

int main(){
 char str[1024];
 int pipefd[2];
 pid_t pid;
 int status, died;

        if (signal (SIGCHLD, popen_sigchld_handler) == SIG_ERR) {
                printf ("Cannot catch SIGCHLD\n");
		_exit(-1);
        }

  pipe (pipefd);
  switch(pid=fork()){
   case -1: 
	    printf("can't fork\n");
            _exit(-1);
   
   case 0 : /* this is the code the child runs */
            close(1);      /* close stdout */
            /* pipefd[1] is for writing to the pipe. We want the output
             * that used to go to the standard output (file descriptor 1)
             * to be written to the pipe. The following command does this,
             * creating a new file descripter 1 (the lowest available) 
             * that writes where pipefd[1] goes. */
            dup (pipefd[1]); /* points pipefd at file descriptor */
            /* the child isn't going to read from the pipe, so
             * pipefd[0] can be closed */
            close (pipefd[0]);

	    /* These are the commands to run, with success commented. dig and nslookup only problems */
            /*execl ("/bin/date","date",0);*/			/* 100% */
	    /*execl ("/bin/cat", "cat", "/etc/hosts", 0);*/ 	/* 100% */
	    /*execl ("/usr/bin/dig", "dig", "redhat.com", 0);*/	/* 69% */
	    /*execl("/bin/sleep", "sleep", "1", 0);*/		/* 100% */
            execl ("/usr/bin/nslookup","nslookup","redhat.com",0); /* 90% (after 100 tests), 40% (after 10 tests) */
            /*execl ("/bin/ping","ping","-c","1","localhost",0);*/	/* 100% */
            /*execl ("/bin/ping","ping","-c","1","192.168.10.32",0);*/	/* 100% */
	    _exit(0);

   default: /* this is the code the parent runs */

            close(0); /* close stdin */
            /* Set file descriptor 0 (stdin) to read from the pipe */
            dup (pipefd[0]);
            /* the parent isn't going to write to the pipe */
            close (pipefd[1]);
            /* Now read from the pipe */
            fgets(str, 1023, stdin);
            /*printf("1st line output is %s\n", str);*/

	    /*while (!childtermd);*/  /* Uncomment this line to fix */

            died= wait(&status);
	    /*printf("died=%d status=%d\n", died, status);*/
	    if (died > 0) _exit(0);
	    else          _exit(1);
   }
}

void
popen_sigchld_handler (int signo)
{
        if (signo == SIGCHLD) {
                /*printf("Caught sigchld\n");*/
                childtermd = 1;
        }
}
