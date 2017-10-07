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
    
} /* start2 */

/* ------------------------------------------------------------------------
 Name - spawn
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */

void spawn(USLOSS_Sysargs *sysargs){
    
    long pid;
    //check args
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
    
    pid = spawnReal((char *) sysargs->arg5, sysargs->arg1, sysargs->arg2, sysargs->arg3, sysargs->arg4);
    
    sysargs->arg1 = (void *) pid;
    sysargs->arg4 = (void *) 0;
    
    // FIXME: SetUserMode();
}


int spawnReal(char *name, int (*startFunc)(char *), char *arg, int stacksize, int priority){
    
    int kidpid = fork1(name, spawnLaunch, arg, stacksize, priority);
    
    // Return -1 if process could not be created
    if (kidpid < 0) {
        return -1;
    }
    
    // Initialize entry in procTable
    // If Parent has higher priority than new child, initilize procTable Entry
    if (p3ProcTable[kidpid % MAXPROC].status == EMPTY ){
        p3ProcTable[kidpid % MAXPROC].mboxID = MboxCreate(0, 0);
        p3ProcTable[kidpid % MAXPROC].status = USED;
    }
    
    // Copy name to procTable entry
    strcpy(p3ProcTable[kidpid % MAXPROC].name, name);
    // Copy func to procTable entry
    p3ProcTable[kidpid % MAXPROC].startFunc = startFunc;
    
    // Copy args to procTable entry
    if (arg == NULL) {
        p3ProcTable[kidpid % MAXPROC].args[0] = 0;
    } else {
        strcpy(p3ProcTable[kidpid % MAXPROC].args, arg);
    }
    
    // Add child to child list FIXME: I may need to add new child to back of list
    p3ProcTable[kidpid % MAXPROC].nextSibling = p3ProcTable[getpid() % MAXPROC].children;
    p3ProcTable[getpid() % MAXPROC].children = &p3ProcTable[kidpid % MAXPROC];
    
    // Cond Send to mailbox
    MboxCondSend(p3ProcTable[kidpid % MAXPROC].mboxID, NULL, 0);
    
    return kidpid;
}


int spawnLaunch(char * args) {
    
    // If child has higher priority than its parent, Create index in proc table, Create MailBox
    int myPID = getpid();
    if (p3ProcTable[myPID % MAXPROC].status == EMPTY) {
        int mboxID =MboxCreate(0, 0);
        p3ProcTable[myPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[myPID % MAXPROC].status = USED;
        MboxReceive(mboxID, NULL, 0);
    }
    // FIXME: SetUserMode();
    // Call the process' startFunc with the given args
    p3ProcTable[myPID % MAXPROC].startFunc(p3ProcTable[myPID % MAXPROC].args);
    
    
}


