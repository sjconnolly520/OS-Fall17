#include "usloss.h"
#include "usyscall.h"
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "sems.h"


#include <string.h>

/*----------Prototypes------------*/

int start2(char*);
extern int start3(char*);

void spawn(USLOSS_Sysargs *);
int spawnReal(char *, int (*startFunc)(char *), char *, int, int );
int spawnLaunch(char *);

void nullsys3(USLOSS_Sysargs *);
int waitReal(int*);
void wait1(USLOSS_Sysargs *);

void terminate(USLOSS_Sysargs *);
void terminateReal(int status);


void setUserMode(void);

/* ----------- Globals ------------- */
p3Proc p3ProcTable[MAXPROC];

int start2(char *arg) {
    int pid;
    int status;
    /*
     * Check kernel mode here.
     */
    
    /*
     * Data structure initialization as needed...
     */
    
    // Initialize systemCallVec array with appropriate system call functions
    // initialize systemCallVec to system call functions
    systemCallVec[SYS_SPAWN] = spawn;
    systemCallVec[SYS_WAIT] = wait1;
    systemCallVec[SYS_TERMINATE] = terminate;
    systemCallVec[SYS_SEMCREATE] = nullsys3;
    systemCallVec[SYS_SEMP] = nullsys3;
    systemCallVec[SYS_SEMV] = nullsys3;
    systemCallVec[SYS_SEMFREE] = nullsys3;
    systemCallVec[SYS_GETPID] = nullsys3;
    systemCallVec[SYS_GETTIMEOFDAY] = nullsys3;
    systemCallVec[SYS_CPUTIME] = nullsys3;
    
    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscallHandler; spawnReal is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes USLOSS_Syscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawnReal().
     *
     * Here, we only call spawnReal(), since we are already in kernel mode.
     *
     * spawnReal() will create the process by using a call to fork1 to
     * create a process executing the code in spawnLaunch().  spawnReal()
     * and spawnLaunch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawnReal() will
     * return to the original caller of Spawn, while spawnLaunch() will
     * begin executing the function passed to Spawn. spawnLaunch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawnReal() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and
     * return to the user code that called Spawn.
     */
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);
    
    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);
    
    quit(0);
    return 0;
} /* start2 */

/* ------------------------------------------------------------------------
 Name - spawn
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */

void spawn(USLOSS_Sysargs *sysargs){
    
    // Chack for any invalid args
    if ((long) sysargs->number != SYS_SPAWN) {
        sysargs->arg4 = (void *) -1;
        return;
    }
    
    if ((long) sysargs->arg3 < USLOSS_MIN_STACK) {
        sysargs->arg4 = (void *) -1;
        return;
    }
    
    if ((long) sysargs->arg4 > MINPRIORITY || (long) sysargs->arg4 < MAXPRIORITY) {
        sysargs->arg4 = (void *) -1;
        return;
    }
    
    long pid = spawnReal((char *) sysargs->arg5, sysargs->arg1, sysargs->arg2, (int) sysargs->arg3, (int) sysargs->arg4);
    
    sysargs->arg1 = (void *) pid;
    sysargs->arg4 = (void *) 0;
    setUserMode();
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - spawnReal
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
int spawnReal(char *name, int (*startFunc)(char *), char *arg, int stacksize, int priority){
    
    int mboxID;
    
    // Fork the new process, when fork returns, save kidpid
    int kidPID = fork1(name, spawnLaunch, arg, stacksize, priority);
    
    // Return -1 if process could not be created
    if (kidPID < 0) {
        return -1;
    }
    
    // Initialize entry in procTable
    // If Parent has higher priority than new child, initilize procTable Entry
    if (p3ProcTable[kidPID % MAXPROC].status == EMPTY ){
        mboxID = MboxCreate(0, 0);
        p3ProcTable[kidPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[kidPID % MAXPROC].status = ACTIVE;
    }
    
    // Copy name to procTable entry, Copy startFunc to procTable entry
    strcpy(p3ProcTable[kidPID % MAXPROC].name, name);
    p3ProcTable[kidPID % MAXPROC].startFunc = startFunc;
    
    //set pid
    p3ProcTable[kidPID % MAXPROC].pid = kidPID;
    
    // Copy args to procTable entry
    if (arg == NULL) {
        p3ProcTable[kidPID % MAXPROC].args[0] = 0;
    } else {
        strcpy(p3ProcTable[kidPID % MAXPROC].args, arg);
    }
    
    // Add child to child list FIXME: I may need to add new child to back of list
    p3ProcTable[kidPID % MAXPROC].nextSibling = p3ProcTable[getpid() % MAXPROC].children;
    p3ProcTable[getpid() % MAXPROC].children = &p3ProcTable[kidPID % MAXPROC];
    
    // Cond Send to mailbox
    MboxCondSend(p3ProcTable[kidPID % MAXPROC].mboxID, NULL, 0);
    
    return kidPID;
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - spawnLaunch
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
int spawnLaunch(char * args) {
    
    // If child has higher priority than its parent, Create index in proc table, Create MailBox
    int myPID = getpid();
    if (p3ProcTable[myPID % MAXPROC].status == EMPTY) {
        int mboxID =MboxCreate(0, 0);
        p3ProcTable[myPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[myPID % MAXPROC].status = ACTIVE;
        MboxReceive(mboxID, NULL, 0);
    }
    setUserMode();
    // Call the process' startFunc with the given args
    p3ProcTable[myPID % MAXPROC].startFunc(p3ProcTable[myPID % MAXPROC].args);
    
    return -404;
}
/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - wait
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void wait1(USLOSS_Sysargs * args){

    //check
    if ((long) args->number != SYS_WAIT) {
        args->arg2 = (void *) -1;
        return;
    }
    
    //call waitReal
    int status;
    int kidPID = waitReal(&status);
    
    p3ProcTable[getpid() % MAXPROC].status = ACTIVE;
    
    //setup args for return
    if (kidPID == -2) {
        args->arg1 = (void *) 0;
        args->arg2 = (void *) -2;
    }
    else {
        args->arg1 = ((void *) (long) kidPID);
        args->arg2 = ((void *) (long) status);
    }
    setUserMode();
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - waitReal
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
int waitReal(int * status) {
    p3ProcTable[getpid() % MAXPROC].status = WAIT_BLOCK;
    return join(status);
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - Terminate
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void terminate(USLOSS_Sysargs * args) {
    //check
    if ((long) args->number != SYS_TERMINATE) {
        args->arg2 = (void *) -1;
        return;
    }
    	
    //call tReal
    if (args->arg1){
    	terminateReal((int)(long)args->arg1);
    }
}

void terminateReal(int status){
	
	int myPID = getpid();
	p3ProcPtr current = &p3ProcTable[myPID % MAXPROC];
	
	//zap loop
	while(current->children != NULL){
		zap(current->children->pid);
		current->children = current->children->nextSibling;
	}

	current->status = EMPTY;
	
	quit(status);
}
void nullsys3(USLOSS_Sysargs *sysargs) {
    
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - setUserMode
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void setUserMode() {
    if (USLOSS_PsrSet(USLOSS_PsrGet() & 0xE) == USLOSS_ERR_INVALID_PSR) {               // 0xE == 14 == 1110
        USLOSS_Console("ERROR: setUserMode(): Failed to chance to User Mode.");
    }
}
