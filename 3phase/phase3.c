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
extern void Terminate(int);

void spawn(USLOSS_Sysargs *);
int spawnReal(char *, int (*startFunc)(char *), char *, int, int );
int spawnLaunch(char *);

void nullsys3(USLOSS_Sysargs *);
int waitReal(int*);
void wait1(USLOSS_Sysargs *);

void terminate(USLOSS_Sysargs *);
void terminateReal(int status);

void semCreate(USLOSS_Sysargs *);

void semP(USLOSS_Sysargs *);

void semV(USLOSS_Sysargs *);

void setUserMode(void);
void addToSemphoreBlockedList(int, int);
void removeFromSemaphoreBlockedList(int,int);

void getpid1(USLOSS_Sysargs *);

void semFree(USLOSS_Sysargs *);
void getTimeOfDay(USLOSS_Sysargs *);
void cputime(USLOSS_Sysargs *);

void zapkids(p3ProcPtr);

/* ----------- Globals ------------- */
p3Proc p3ProcTable[MAXPROC];
semStruct semStructTable[MAXSEMS];

/* ----------- Functions ------------- */

/* ------------------------------------------------------------------------
 Name - start2
 Purpose     - Initialized all of the required global variables for a function run.
             - Spawns a process called 'start3'
 Parameters  - char * arg : may be usless, I'm not certain
 Returns     - 0 - This indicates something went wrong.
             - nothing - quit has been called successfully
 Side Effects - ProcTable is initialized, SemTable is initialized, sysCallVec is initialized
 ----------------------------------------------------------------------- */
int start2(char *arg) {
    int pid;
	int status;
    
    // Check kernel mode here.
    if (!(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)) {
        USLOSS_Console("start2(): called while in user mode. ");
        USLOSS_Console("Halting...\n");
        USLOSS_Halt(1);
    }
    
    // Initialize Process Table Entries
    for(int i = 0; i < MAXPROC; i++){
    	p3ProcTable[i].status 			= EMPTY;
    	p3ProcTable[i].mboxID 			= -1;
    	p3ProcTable[i].pid 				= -1;
    	
    	p3ProcTable[i].children 		= NULL;
    	p3ProcTable[i].nextSibling 		= NULL;
    	p3ProcTable[i].nextSemBlocked 	= NULL;
    }
    
    // Initialize Semaphore Table Entries
    for(int i = 0; i < MAXSEMS; i++){
    	semStructTable[i].status 	= EMPTY;
    	semStructTable[i].mboxID 	= -1;
    	semStructTable[i].flags 	= -1;
    	semStructTable[i].blockList = NULL;
    }
    
    // Initialize systemCallVec array to nullsys3
    for(int i = 0; i < USLOSS_MAX_SYSCALLS; i++){
    	systemCallVec[i] = nullsys3;
    }
    // Initialize systemCallVec with appropriate system call functions
    systemCallVec[SYS_SPAWN] = spawn;
    systemCallVec[SYS_WAIT] = wait1;
    systemCallVec[SYS_TERMINATE] = terminate;
    systemCallVec[SYS_SEMCREATE] = semCreate;
    systemCallVec[SYS_SEMP] = semP;
    systemCallVec[SYS_SEMV] = semV;
    systemCallVec[SYS_SEMFREE] = semFree;
    systemCallVec[SYS_GETPID] = getpid1;
    systemCallVec[SYS_GETTIMEOFDAY] = getTimeOfDay;
    systemCallVec[SYS_CPUTIME] = cputime;
    
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
    
    // Fork the start3 process
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);

    
    // If failed to create start3 process
    if (pid < 0) {
        quit(pid);
    }
    
    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);
    
    // If failed to join with start3 child process
    if (pid < 0) {
        quit(pid);
    }
    
    quit(0);
    return 0;
} /* start2 */

/* ------------------------------------------------------------------------
 Name - spawn
 Purpose     - kernel mode version of Spawn. Performs the error checking
               for spawnReal.
 Parameters  - USLOSS_Systemarg - parameters from the user.
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
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
    
    // Call the actual initialization function for the new process
    long pid = spawnReal((char *) sysargs->arg5, sysargs->arg1, sysargs->arg2, (long) sysargs->arg3, (long) sysargs->arg4);
    
    // Set proper output and set back to user mode
    sysargs->arg1 = (void *) pid;
    sysargs->arg4 = (void *) 0;
    setUserMode();
} /* spawn */

/* ------------------------------------------------------------------------
 Name - spawnReal
 Purpose     - Phase3 version of fork; makes a call to fork1.
             - Initializes the process table entry for a new child process.
 Parameters  - char * name : the name of the new child process
             - int (*startFunc)(char *) : a pointer to the new child process' startFunction
 Returns     - int : the pid of the newly created child process
 Side Effects - a new process in the process table is initialized.
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
    // If Parent has higher priority than new child, initialize process mailbox
    if (p3ProcTable[kidPID % MAXPROC].status == EMPTY ){
        mboxID = MboxCreate(0, 0);
        p3ProcTable[kidPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[kidPID % MAXPROC].status = ACTIVE;
    }
    
    // Copy name to procTable entry, Copy startFunc to procTable entry
    strcpy(p3ProcTable[kidPID % MAXPROC].name, name);
    p3ProcTable[kidPID % MAXPROC].startFunc = startFunc;
    
    // Set pid
    p3ProcTable[kidPID % MAXPROC].pid = kidPID;
    
    // Copy args to procTable entry
    if (arg == NULL) {
        p3ProcTable[kidPID % MAXPROC].args[0] = 0;
    } else {
        strcpy(p3ProcTable[kidPID % MAXPROC].args, arg);
    }
    
    // Add child to end of child list
    p3ProcPtr walk = p3ProcTable[getpid() % MAXPROC].children;
    // If process child is the parent's first child
    if (walk == NULL){
    	p3ProcTable[getpid() % MAXPROC].children = &p3ProcTable[kidPID % MAXPROC];
    }else{ // Otherwise, insert at the end
    	while(walk->nextSibling != NULL){
    		walk = walk->nextSibling;
    	}
    	walk->nextSibling = &p3ProcTable[kidPID % MAXPROC];
    }
       
    // Cond Send to mailbox
    MboxCondSend(p3ProcTable[kidPID % MAXPROC].mboxID, NULL, 0);
    
    // Return the child's PID
    return kidPID;
} /* spawnReal */

/* ------------------------------------------------------------------------
 Name - spawnLaunch
 Purpose     - Phase3 version of launch. Calls the process' startFunc
 Parameters  - char * args : May be unnecessary. I'm not certain
 Returns     - int 404 : This indicates something went wrong.
             - nothing : the process returned from its program run, terminated
 Side Effects - none
 ----------------------------------------------------------------------- */
int spawnLaunch(char * args) {
    
    int myPID = getpid();
    
    // If child has higher priority than its parent, Initialize MailBox
    if (p3ProcTable[myPID % MAXPROC].status == EMPTY) {
        int mboxID = MboxCreate(0, 0);
        p3ProcTable[myPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[myPID % MAXPROC].status = ACTIVE;
        MboxReceive(mboxID, NULL, 0);
    }
    
    // If the process was killed while blocked, terminate it.
    if (isZapped() ||p3ProcTable[myPID % MAXPROC].status == EMPTY ){
    	terminateReal(WASZAPPED);
    }
    
    // Return to UserMode before calling the process' function
    setUserMode();
    
    // Call the process' startFunc with the given args
    int result = p3ProcTable[myPID % MAXPROC].startFunc(p3ProcTable[myPID % MAXPROC].args);
    
    // After the process has finished it run, terminate it with the returned value;
    Terminate(result);
    
    // Should never reach here.
    return -404;
} /* spawnLaunch */

/* ------------------------------------------------------------------------
 Name - wait1
 Purpose     - Phase3 version of join; Waits for a child process to finish
 Parameters  - USLOSS_Systemarg : arguments given by the user
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void wait1(USLOSS_Sysargs * args){

    // Check to make sure the arguments passed in are valid
    if ((long) args->number != SYS_WAIT) {
        args->arg2 = (void *) -1;
        return;
    }
    
    // Call waitReal
    int status;
    int kidPID = waitReal(&status);
    
    // If the process was zapped while blocked on a join(), terminate
    if (kidPID == -1) {
        terminateReal(WASZAPPED);
    }
    
    // Reset process table entry
    p3ProcTable[getpid() % MAXPROC].status = ACTIVE;
    
    // Set proper output and set back to user mode
    // If the process joined with code -2 (no children to join)
    if (kidPID == -2) {
        args->arg1 = (void *) 0;
        args->arg2 = (void *) -2;
    }
    // Otherwise, the wait1 was successful
    else {
        args->arg1 = ((void *) (long) kidPID);
        args->arg2 = ((void *) (long) status);
    }
    
    // If the process was zapped while waiting, terminate it
    if (isZapped()){
        terminateReal(WASZAPPED);
    }
    
    // Return to UserMode before returning
    setUserMode();
} /* wait1 */

/* ------------------------------------------------------------------------
 Name - waitReal
 Purpose     - Blocks a user lever process until its child has finished
 Parameters  - int * status : a pointer to the child's return status
 Returns     - int : the pid of the child that finished first
 Side Effects - none
 ----------------------------------------------------------------------- */
int waitReal(int * status) {
    p3ProcTable[getpid() % MAXPROC].status = WAIT_BLOCK;
    return join(status);
}

/* ------------------------------------------------------------------------
 Name - terminate
 Purpose     - A store front function for terminateReal. Checks parameters.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void terminate(USLOSS_Sysargs * args) {
    // Check to make sure the arguments passed in are valid
    if ((long) args->number != SYS_TERMINATE) {
        args->arg2 = (void *) -1;
        return;
    }
    	
    // Make call to terminate real
    terminateReal((int)(long)args->arg1);
    
}

/* ------------------------------------------------------------------------
 Name - terminateReal
 Purpose     - Zaps a process' children, waits for them to terminate.
             - Quits the calling process once children have zapped.
 Parameters  - int status : quit status of the calling process
 Returns     - nothing
 Side Effects - Sets process table entries for children and current to EMPTY
 ----------------------------------------------------------------------- */
void terminateReal(int status){
	
    // Grab the currently running process' slot in the process table
	int myPID = getpid();
	p3ProcPtr current = &p3ProcTable[myPID % MAXPROC];

	// Zap all of the calling process' active children
	zapkids(current->children);
	// while(current->children != NULL){
// 		if (current->children->status == ACTIVE){
// 			current->children->status = EMPTY;
// 			
// 			zap(current->children->pid);
// 		}
// 		current->children = current->children->nextSibling;
// 	}

    // Destroy the entry in the process table for the currently running process and quit
    current->status = EMPTY;
	quit(status);
} /* terminateReal */

/* I think this function is just a place holder. */
/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - nullsys3
 Purpose     - Place Holder. Does nothing. I think.
 Parameters  -
 Returns     -
 Side Effects -
 ----------------------------------------------------------------------- */
void nullsys3(USLOSS_Sysargs *sysargs) {
    //checking?
    terminateReal(1);
} /* nullsys3 */

/* ------------------------------------------------------------------------
 Name - semCreate
 Purpose     - Creates a semaphore. Initializes the first available entry in the semTable
 Parameters  - USLOSS_Systemarg : User parameters for the new semaphore
 Returns     - nothing
 Side Effects - Changes values for an entry in the semTable
              - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void semCreate(USLOSS_Sysargs * args) {
    
    // If semaphore flags is negative, fail to create semaphore
    if ((long) args->arg1 < 0) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Initialize semaphore values: value, blocked list, status, mailbox
    int semIndex;
    for(semIndex = 0; semIndex < MAXSEMS; semIndex++) {
        if (semStructTable[semIndex].status == EMPTY) {
            semStructTable[semIndex].status = ACTIVE;
            semStructTable[semIndex].flags = (long) args->arg1;
            semStructTable[semIndex].mboxID = MboxCreate(1, 0);
            break;
        }
    }
    
    // If no more available slots in semTable, return
    if (semIndex >= MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Set output (if semCreate succeeded)
    args->arg4 = ((void *) (long) 0);
    args->arg1 = ((void *) (long) semIndex);
    
    // If calling process was zapped
    if (isZapped()) {
        terminateReal(WASZAPPED);
    }
    
    // Return to User Mode
    setUserMode();
} /* semCreate */

/* Probably don't need this. A relic of a more elegant age. */
/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - semCreateReal
 Purpose     - kernel mode version of spawn.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void semCreateReal() {
    
}  /* semCreateReal */

/* ------------------------------------------------------------------------
 Name - semP
 Purpose     - Perform a 'P' operation on a semaphore.
             - Decreases the number of flags in semaphore.
             - Blocks a process if the number of flags is < 0.
 Parameters  - USLOSS_Systemarg : Contains the identifier of the semaphore
 Returns     - nothing
 Side Effects - Potentially blocks the calling process on the semaphore
              - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void semP(USLOSS_Sysargs * args) {
    // Error checking. If index out of range, return.
    int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Error checking. If not a valid semaphore, return.
    if (semStructTable[semIndex].status != ACTIVE) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Enter critical section of code. Gain Mutex.
    int mutex_mboxID = semStructTable[semIndex].mboxID;
    MboxSend(mutex_mboxID, NULL, 0);
    
    // Decrement number of flags
    semStructTable[semIndex].flags--;
    
    // If number of flags < 0, block the calling process on its private mailbox
    if(semStructTable[semIndex].flags < 0){
        
        // Add process to semaphore block list
    	addToSemphoreBlockedList(getpid(), semIndex);
        
        // Release Mutex. Call Send() to block on private mailbox.
    	MboxReceive(mutex_mboxID, NULL, 0);
    	MboxSend(p3ProcTable[getpid() % MAXPROC].mboxID, NULL, 0);
    }
    else{       // If we are allowed to enter critical section, do not block
        // Release Mutex, allow process to continue program run.
    	MboxReceive(mutex_mboxID, NULL, 0);
    }
    
    // Set output
    args->arg4 = 0;
    
    // If process was zapped while blocked on the semaphore, terminate
    if (isZapped() || semStructTable[semIndex].status == EMPTY){
    	terminateReal(WASZAPPED);
    }
    
    // Return to User Mode
    setUserMode();
}  /* semP */

/* ------------------------------------------------------------------------
 Name - addToSemphoreBlockedList
 Purpose     - Adds a given process to the blocked list of a given semaphore
 Parameters  - int pid : the pid of the process to add to the block list
             - int semIndex : the index in the semTable of the semaphore
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void addToSemphoreBlockedList(int pid, int semIndex){
    
    // Create a pointer to the process to be added to the block list
	p3ProcPtr toInsert  = &p3ProcTable[pid % MAXPROC];
    
	p3ProcPtr walk = semStructTable[semIndex].blockList;
    
    // If there are no other processes blocked on the semaphore, PID becomes the first
    if (walk == NULL) {
        semStructTable[semIndex].blockList = toInsert;
    }
    // If there are processes already blocked on the semaphore, add the new one to the end of the list
	else{
		while(walk->nextSemBlocked != NULL){
			walk = walk->nextSemBlocked; 
		}	
		walk->nextSemBlocked = toInsert;
	}
} /* addToSemaphoreBlockedList */

/* ------------------------------------------------------------------------
 Name - semV
 Purpose     - Perform a 'V' operation on a given semaphore.
             - Increases the number of flags on semaphore.
             - Unblocks the first process blocked on the semaphore (if one exists)
 Parameters  - USLOSS_Systemarg : Contains the identifier of the semaphore
 Returns     - nothing
 Side Effects - Potentially unblocks a process that was blocked on the semaphore
              - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void semV(USLOSS_Sysargs * args) {
    
    // Error checking. If index out of range, return.
    int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Error checking. If not a valid semaphore, return.
    if (semStructTable[semIndex].status != ACTIVE) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Enter critical section of code. Gain mutex.
    int mutex_mboxID = semStructTable[semIndex].mboxID;
    MboxSend(mutex_mboxID, NULL, 0);
    
    // If there are processes blocked on the semaphore
    if(semStructTable[semIndex].flags < 0){
        // Increase the number of flags on the semaphore.
    	semStructTable[semIndex].flags++;
        
        // Remove the first blocked process from blocked list
    	int blockedPID = semStructTable[semIndex].blockList->pid;
    	semStructTable[semIndex].blockList = semStructTable[semIndex].blockList->nextSemBlocked;
        
        // Unblock the process. Receive on the blocked process' private mailbox
    	MboxReceive(p3ProcTable[blockedPID % MAXPROC].mboxID, NULL, 0);
        
        // Release mutex.
    	MboxReceive(mutex_mboxID, NULL, 0);
    }
    else{               // If there are no blocked processes
        // Increase the number of flags. Release mutex.
    	semStructTable[semIndex].flags++;
    	MboxReceive(mutex_mboxID, NULL, 0);
    }
    
    // Set output for User
    args->arg4 = 0;
    
    // If process has been zapped, terminate.
    if (isZapped()) {
        terminateReal(WASZAPPED);
    }
    
    // Return to User Mode.
    setUserMode();
} /* semV */

/* ------------------------------------------------------------------------
 Name - setUserMode
 Purpose     - Sets the PSR to UserMode, (Sets first bit to 0)
 Parameters  - none
 Returns     - nothing
 Side Effects - Sets PSR Mode Bit to 0
 ----------------------------------------------------------------------- */
void setUserMode() {
    if (USLOSS_PsrSet(USLOSS_PsrGet() & 0xE) == USLOSS_ERR_INVALID_PSR) {               // 0xE == 14 == 1110
        USLOSS_Console("ERROR: setUserMode(): Failed to change to User Mode.\n");
    }
}  /* setUserMode */

/* ------------------------------------------------------------------------
 Name - getpid1
 Purpose     - Phase3 implementation of getPID.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing (Systemarg.arg1 contains the current process' PID)
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void getpid1(USLOSS_Sysargs * args){
    
    // Set output
	args->arg1 = (void *)(long)getpid();
	
    // If process was zapped, terminate it.
    if (isZapped()){
        terminateReal(WASZAPPED);
    }

    // Return to User Mode
	setUserMode();
}  /* getpid1 */

/* ------------------------------------------------------------------------
 Name - semFree
 Purpose     - Destroys entry in semTable
 Parameters  - USLOSS_Systemarg : Contains the identifier of the semaphore
 Returns     - nothing
 Side Effects - Sets given entry in the semaphore table to EMPTY
              - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void semFree(USLOSS_Sysargs * args){
    
    // Error checking. If index out of range, return.
	int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // Error checking. If not a valid semaphore, return.
    if (semStructTable[semIndex].status == EMPTY ) {
        args->arg4 = ((void *) (long) -1);
        return;
    }

    // Set semTable entry status to EMPTY
    semStructTable[semIndex].status = EMPTY;
    
    // If there were processes blocked on the semaphore, unblock them
    if (semStructTable[semIndex].flags < 0){
        
        // Unblock all blocked processes
    	p3ProcPtr walk = semStructTable[semIndex].blockList;
		while(walk != NULL){
			p3ProcPtr temp = walk->nextSemBlocked;
			MboxReceive(walk->mboxID, NULL, 0);
			walk = temp; 
		}
        
        // If there were processes blocked on the semaphore, return 1
		args->arg4 = ((void *) (long) 1);
	}
    else {
        // If there were no processes blocked on the semaphore, return 0
		args->arg4 = ((void *) (long) 0);
	}
   
    // If process was zapped, terminate it.
    if (isZapped()){
        terminateReal(WASZAPPED);
    }
   
    // Return to User Mode
   	setUserMode();
}  /* semFree */

/* ------------------------------------------------------------------------
 Name - getTimeOfDay
 Purpose     - Phase3 version of currentTime.
             - Returns amount of time USLOSS has been running
 Parameters  - USLOSS_Systemarg : Used only for returning
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void getTimeOfDay(USLOSS_Sysargs * args){
    
    // Get the time that USLOSS has been running.
	int currentTime;
	if (USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &currentTime) != USLOSS_DEV_OK){
    	USLOSS_Console("USLOSS_DeviceInput() != USLOSS_DEV_OK \n");
	}
    
    // Set the return value to be the USLOSS runtime.
	args->arg1 = (void *)(long)currentTime;
	
    // If the process was zapped, terminate it.
    if (isZapped()) {
        terminateReal(WASZAPPED);
    }
	
    // Return to User Mode
	setUserMode();
}  /* getTimeOfDay */

/* ------------------------------------------------------------------------
 Name - cputime
 Purpose     - Phase3 version of readtime.
             - Returns the amount of time the current process has had on the CPU
 Parameters  - USLOSS_Systemarg : Used for returning only
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
void cputime(USLOSS_Sysargs * args){
    
    // Set the return value to be the CPU time used
	args->arg1 = (void *)(long)readtime();
	
    // If the process was zapped, terminate it.
    if (isZapped()) {
        terminateReal(WASZAPPED);
    }
	
    // Return to User Mode
	setUserMode();
}  /* cputime */

// FIXME: Block Comment && Inline comments
/* ------------------------------------------------------------------------
 Name - zapkids
 Purpose     - Recursively zaps all children
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void zapkids(p3ProcPtr children) {
	if (children == NULL) return;
	
	p3ProcPtr walk = children;
	while( walk != NULL){
		zapkids(walk->children);
        if (walk->status == ACTIVE) {
            zap(walk->pid);
        }
        p3ProcPtr temp = walk;
		walk = walk->nextSibling;
        temp->nextSibling = NULL;
	}
	
} /* zapkids */
