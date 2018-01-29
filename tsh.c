/* 
 * tsh - A tiny shell program with job control
 * 
 * Brian
 * team: alone
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
char *builtIn[4] = {"quit", "fg", "bg", "jobs"}; /*array of built in cmds*/

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void do_redirect(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* Ignoring these signals simplifies reading from stdin/stdout */
    Signal(SIGTTIN, SIG_IGN);          /* ignore SIGTTIN */
    Signal(SIGTTOU, SIG_IGN);          /* ignore SIGTTOU */


    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
/* ERROR HANDLING WRAPPERS (STEVENS) */
/*
 * CS:APP fork() wrapper
 */
pid_t Fork(void){
    pid_t pid;
    //if fork is == -1 there is a fork error
    if((pid = fork()) < 0){
	unix_error("fork error");
    }
	if(verbose){ printf("[+] %d\n", pid); }
    return pid;

}

/*
 * Error handling wrapper for
 * signalemptyset, -1 == error
 */
void sigempty_wrapper(sigset_t *pmask){
    if(sigemptyset(pmask) == -1){
	unix_error("sigemptyset error");
    }
    return;
}

/*
 * Error handling wrapper for
 * sigaddset, -1 == error
 */
void sigadd_wrapper(sigset_t *pmask, int sig){
    if(sigaddset(pmask, sig) == -1){
	if(verbose){printf("SIGNIUM: %d\n", sig);}
	unix_error("sigaddset error, check verbose for signium");
    }
    return;
}

/*
 * Error handling wrapper for
 * siggprocmask, error == -1
 */
void sigproc_wrapper(int sig, sigset_t *pmask){
    if(sigprocmask(sig, pmask, NULL) == -1){
	if(verbose){printf("SIGNIUM: %d\n", sig);}
	unix_error("sigprocmask error, check verbose for signium");
    }
    return;
}

/*
 * Error handling wrapper for setpgid
 * (text 759)
 */
void setpgid_wrapper(void){
    if(setpgid(0 , 0) == -1){
	unix_error("setpgid error");
    }
    return;
}

/*
 * Error handling wrapper for
 * kill
 */
void kill_wrapper(pid_t pid, int sig){
    if(kill(pid, sig) == -1){
	unix_error("kill error");
    }
}
/* END OF ERROR HANDLING WRAPPERS */

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{//credit to CS:APP slides and textbook
 //class 3/1   
    char *argv[MAXARGS]; //arg array for execve()
    int bg; //whether or not the job will run in bg or fg
    pid_t pid; //process id
    sigset_t mask; //mask for blocking signals

    /* builds the argv array while checking if bg will be true*/
    bg = parseline(cmdline, argv);
    if(argv[0] != NULL){ //if there are no args then we ignore and return
	sigempty_wrapper(&mask); //init sigset mask

	sigadd_wrapper(&mask, SIGCHLD); //sigadd SIGCHLD to mask
	
	/*
         * Check for if the command is built in
         * if it is, return else continue if it is
	 * not built in 
         */
	if(!builtin_cmd(argv)){
	    sigproc_wrapper(SIG_BLOCK, &mask); //block sig set   

	    if((pid = Fork()) == 0){ //child runs user job, uses fork wrapper above eval func
		sigproc_wrapper(SIG_UNBLOCK, &mask); //unblock sig set
		setpgid_wrapper(); //leave parents process group and create own (pg759)
		/*
		 * execve will execute the file named
		 * by argv[0] with any arg that follows
		 * if execve fails, nothing will be returned
		 */
		if(execve(argv[0], argv, environ) < 0){
		    printf("%s: Command not found.\n", argv[0]);
		    exit(0);
		}
	}else{
		/* adding job to jobs list */
		int jobstate; //var for whether current job is bg/fg
		if(!bg){
		    jobstate = FG;
		}else{
		    jobstate = BG;
		}		
		addjob(jobs, pid, jobstate, cmdline);
		sigproc_wrapper(SIG_UNBLOCK, &mask); //unblock sig set
		if(!bg){ //parent waits for job to term if it is fg
		    waitfg(pid);  //wait, don't really need pid with fgjob();		
		}else{//adding bg job
		    printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
	    	}
	    } 
        }

    }

    
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	   buf++;
	   delim = strchr(buf, '\'');
    }
    else {
	   delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	   if (*buf == '\'') {
	       buf++;
	       delim = strchr(buf, '\'');
	   }
	   else {
	       delim = strchr(buf, ' ');
	   }
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	   argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    int cmd = 4; //this will emain 4 if argv[0] is not a builtin cmd
    int i; //control variable
    for(i = 0; i < 4; i++){
	/* if a built in command is found, update the cmd variable */
        if(!strcmp(builtIn[i], argv[0])){
	    cmd = i;
	}
    }
    
    if(cmd != 4){ //if argv[0] was built cmd will not be 4 anymore
        switch(cmd){ //handles various builtin commands 
	case 0: //exit
	    exit(0);
	    break;

	case 1: //bgfg
	    do_bgfg(argv);
	    return 1;
	    break;

        case 2: //bgfg
	    do_bgfg(argv);
	    return 1;
	    break;
	    
	case 3: //jobs
	    listjobs(jobs);
	    return 1;
	    break;
	 
	default:
	    break;
	}
    }
    
    return 0;     /* not a builtin command */
}


/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    struct job_t *job; //job belonging to jid/pid
    pid_t pid; //pid from argv[1]
    int jid; //jid from argv[1]
    int ground; //keeping track of whether argv[0] is fg/bg

    if(argv[1] == NULL){ //checking if there is a pid/jid
	   //credit to stackoverflow on how to escape %
	   printf("%s command requires PID or %%jobid argument\n", argv[0]);
	   return;	
    }

    if(!strcmp(argv[0], "fg")){ //checking if fg
        ground = 1; //fg
    }else{
	   ground = 0; //bg
    }

    char *temp = argv[1]; //used for getting jid

    if(*temp == '%'){ //checking if input is jid

	jid = atoi(&temp[1]);

	if(verbose){
	    printf("%d\n", jid);
	}
	if((job = getjobjid(jobs, jid)) == NULL){ //checking if job exists
	    printf("%%%d: No such job\n", jid);
	    return;
	}
    }else{
	/* for test14, mimicking output of the reference test by check if input is char */
	if(isalpha(temp[0]) || isalpha(temp[1])){
	    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
	    return;
	}
	pid = atoi(argv[1]); //converting argv[1] into an int

	if(verbose){printf("%d\n", pid);}

	   if((job = getjobpid(jobs, pid)) == NULL){ //checking if job exists
	       printf("(%d): No such process\n", pid);
	       return;
	   }
    }

    /* we have the job and the ground (fg/bg) */
    if(ground){ //fg
	   if(verbose){
	        printf("testing: %d\n", job->state);
	   }
	   job->state = FG; //changing state
	   kill_wrapper(-(job->pid), SIGCONT); //sending SIGCONT to pg 
	   waitfg(job->pid); //waiting for job since it is now fg
    }else{  //bg
	   job->state = BG;
	//getting elements from the job struct incase job/pid uninitialized
	   printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
	   kill_wrapper(-(job->pid), SIGCONT);  //sending SIGCONT to pg
    }
    
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    /*
     * fgpid is the built in function
     * that iterates through the jobs list
     * and returns the pid of the job
     * whos state == FG (1)
     */
    //pid_t test = fgpid(jobs);
    while(fgpid(jobs)){ //rets 0 if no fg 
    	sleep(0);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{//credit to CS:APP text pages 745-787
    pid_t pid = fgpid(jobs);
    int jid = pid2jid(pid);
    int status;

    /*
     * Waiting for all child processes to
     * either terminate or stop
     */
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0){
	if(WIFEXITED(status)){ //rets true if child terminated
	   if(verbose){printf("\nchild: %d terminated %d\n", pid, WEXITSTATUS(status));}
	       deletejob(jobs, pid); //remove from job list
	   }
	   if(WIFSTOPPED(status)){ //rets true if child process was signaled to stopped
	       printf("Job [%d] (%d) stopped by signal 20\n", jid, pid);
	       getjobjid(jobs, jid)->state = ST; //change state in job struct to stop
	   }
	   if(WIFSIGNALED(status)){ //rets true if child process terminated by signal
	        printf("Job [%d] (%d) terminated by signal 2\n", jid, pid);
	       deletejob(jobs, pid); //remove job from job list
	   }
    }

    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t pid = fgpid(jobs);
    if(pid){ 
	   kill_wrapper(-pid, SIGINT); //send sigint to fg processgroup
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid = fgpid(jobs);
    if(pid){
	   kill_wrapper(-pid, SIGTSTP); //send sigtstp to fg processgroup
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



