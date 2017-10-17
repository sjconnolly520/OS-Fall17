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

int start2(char *arg) {
    int pid;
	int status;
    /*
     * Check kernel mode here.
     */
    
    /*
     * Data structure initialization as needed...
     */
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
    
    // failed to join with start3 child process
    if (pid < 0) {
        quit(pid);
    }
    
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
    

    long pid = spawnReal((char *) sysargs->arg5, sysargs->arg1, sysargs->arg2, (long) sysargs->arg3, (long) sysargs->arg4);
    
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
    //p3ProcTable[kidPID % MAXPROC].nextSibling = p3ProcTable[getpid() % MAXPROC].children;
    //p3ProcTable[getpid() % MAXPROC].children = &p3ProcTable[kidPID % MAXPROC];
    
    p3ProcPtr walk = p3ProcTable[getpid() % MAXPROC].children;
    if (walk == NULL){
    	p3ProcTable[getpid() % MAXPROC].children = &p3ProcTable[kidPID % MAXPROC];
    }else{
    	while(walk->nextSibling != NULL){ //1 2 3 
    		walk = walk->nextSibling;
    	}
    	walk->nextSibling = &p3ProcTable[kidPID % MAXPROC];
    }
       
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
        int mboxID = MboxCreate(0, 0);
        p3ProcTable[myPID % MAXPROC].mboxID = mboxID;
        p3ProcTable[myPID % MAXPROC].status = ACTIVE;
        MboxReceive(mboxID, NULL, 0);
    }
    
    if (isZapped() ||p3ProcTable[myPID % MAXPROC].status == EMPTY )
    	terminateReal(WASZAPPED);
    setUserMode();
    // Call the process' startFunc with the given args
    int result = p3ProcTable[myPID % MAXPROC].startFunc(p3ProcTable[myPID % MAXPROC].args);
    
    Terminate(result);
    return -404;
}
/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - wait1
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
    
    //zapped while blocked on a join()
    if (kidPID == -1) {
        terminateReal(WASZAPPED);
    }
    
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
    if (isZapped()) terminateReal(WASZAPPED);
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
 Name - terminate
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

    current->status = EMPTY;
	quit(status);
}
void nullsys3(USLOSS_Sysargs *sysargs) {
    
}


void semCreate(USLOSS_Sysargs * args) {
    // Error checking. Out of range, all sems used.
    // If semaphore flags is negative, fail to create semaphore
    if ((long) args->arg1 < 0) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
//    USLOSS_Console("HELLO");
    
    // Initialize semaphore values: value, blocked list, status (mailbox?)
    int semIndex;
    for(semIndex = 0; semIndex < MAXSEMS; semIndex++) {
        if (semStructTable[semIndex].status == EMPTY) {
            semStructTable[semIndex].status = ACTIVE;
            semStructTable[semIndex].flags = (long) args->arg1;
            semStructTable[semIndex].mboxID = MboxCreate(1, 0);
            break;
        }
    }
    
    // No more available slots in semTable
    if (semIndex >= MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
//    USLOSS_Console("Index in SemTable is %d, PID is %d\n", semIndex, getpid());
    
    // --- Output
    args->arg4 = ((void *) (long) 0);
    args->arg1 = ((void *) (long) semIndex);
    
    setUserMode();
}

void semCreateReal() {
    
}

void semP(USLOSS_Sysargs * args) {
    // --- Error checking. Out of range, sem not active.
    int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    if (semStructTable[semIndex].status != ACTIVE) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // --- Enter critical section of code. Mutex send.
    int mutex_mboxID = semStructTable[semIndex].mboxID;
    
    MboxSend(mutex_mboxID, NULL, 0);
    
    semStructTable[semIndex].flags--;
    
    if(semStructTable[semIndex].flags < 0){
    	addToSemphoreBlockedList(getpid(), semIndex); 
    	MboxReceive(mutex_mboxID, NULL, 0);
    	MboxSend(p3ProcTable[getpid() % MAXPROC].mboxID, NULL, 0);
    }else{
    	MboxReceive(mutex_mboxID, NULL, 0);
    }
    
    
    // --- Output
    args->arg4 = 0;
    if (isZapped() || semStructTable[semIndex].status == EMPTY){
    	terminateReal(WASZAPPED);
    }
    setUserMode();
}

void addToSemphoreBlockedList(int pid, int semIndex){
	p3ProcPtr inInsert  = &p3ProcTable[pid % MAXPROC];
	p3ProcPtr walk = semStructTable[semIndex].blockList;
	if (walk == NULL) semStructTable[semIndex].blockList = inInsert;
	else{
		while(walk->nextSemBlocked != NULL){
			walk = walk->nextSemBlocked; 
		}	
		walk->nextSemBlocked = inInsert;
	}
}
void semV(USLOSS_Sysargs * args) {
    // --- Error checking. Out of range, sem not active.
    int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    if (semStructTable[semIndex].status != ACTIVE) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // --- Enter critical section of code. Mutex send.
    int mutex_mboxID = semStructTable[semIndex].mboxID;
    
    MboxSend(mutex_mboxID, NULL, 0);
    // --- Value += 1
    if(semStructTable[semIndex].flags < 0){
    	semStructTable[semIndex].flags++;
    	int blockedPID = semStructTable[semIndex].blockList->pid;
    	semStructTable[semIndex].blockList = semStructTable[semIndex].blockList->nextSemBlocked;
    	MboxReceive(p3ProcTable[blockedPID % MAXPROC].mboxID, NULL, 0);
    	MboxReceive(mutex_mboxID, NULL, 0);
    }else{
    	semStructTable[semIndex].flags++;
    	MboxReceive(mutex_mboxID, NULL, 0);
    }
    // --- Enter critical section of code. Mutex send.
    
    // --- If process blocked on semaphore, unblock first
        // --- Remove first from block list
        // --- Mutex release
        // --- Send to blockedProc's private mailbox
    // --- Else, release mutex
    
    // --- Output
    args->arg4 = 0;
    setUserMode();
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - setUserMode
 Purpose     - kernel mode version of setUserMode.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void setUserMode() {
    if (USLOSS_PsrSet(USLOSS_PsrGet() & 0xE) == USLOSS_ERR_INVALID_PSR) {               // 0xE == 14 == 1110
        USLOSS_Console("ERROR: setUserMode(): Failed to change to User Mode.\n");
    }
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - getpid1
 Purpose     - kernel mode version of getpid.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void getpid1(USLOSS_Sysargs * args){
	args->arg1 = (void *)(long)getpid();
	setUserMode();
}

/* ------------------------------------------------------------------------ FIXME: Block comment
 Name - semFree
 Purpose     - kernel mode version of semFree.
 Parameters  - USLOSS_Systemarg
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void semFree(USLOSS_Sysargs * args){
	int semIndex = (int)(long)args->arg1;
    if (semIndex < -1 || semIndex > MAXSEMS) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    if (semStructTable[semIndex].status == EMPTY ) {
        args->arg4 = ((void *) (long) -1);
        return;
    }
    
    // --- Enter critical section of code. Mutex send.
    //int mutex_mboxID = semStructTable[semIndex].mboxID;
    
    semStructTable[semIndex].status = EMPTY;
    
    if (semStructTable[semIndex].flags < 0){
    	p3ProcPtr walk = semStructTable[semIndex].blockList;
		while(walk != NULL){
			p3ProcPtr temp = walk->nextSemBlocked;
			MboxReceive(walk->mboxID, NULL, 0);
			walk = temp; 
		}	
		args->arg4 = ((void *) (long) 1);
	}else {
		args->arg4 = ((void *) (long) 0);
	}
   
   	setUserMode();
}

void getTimeOfDay(USLOSS_Sysargs * args){
	int currentTime;
	if (USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &currentTime) != USLOSS_DEV_OK){
    	USLOSS_Console("USLOSS_DeviceInput() != USLOSS_DEV_OK \n");
	}
	args->arg1 = (void *)(long)currentTime;
	setUserMode();
}

void cputime(USLOSS_Sysargs *args){
	args->arg1 = (void *)(long)readtime();
	setUserMode();
}

void zapkids(p3ProcPtr children){
	if (children == NULL) return;
	
	p3ProcPtr walk = children;
	while( walk != NULL){
		zapkids(walk->children);
		if (walk->status == ACTIVE) zap(walk->pid);
		walk = walk->nextSibling;
	}
	
}
