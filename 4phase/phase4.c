#include "usloss.h"
#include "phase1.h"
#include "phase2.h"
#include "libuser.h"
#include "usyscall.h"
#include "phase3.h"
#include "driver.h"
#include "phase4.h"
#include "providedPrototypes.h"
#include <stdlib.h> /* needed for atoi() */
#include <stdio.h>
#include <string.h>
//sems for the drivers etc
int 	clockSemID;

/* ----------- Globals ------------- */
p4Proc p4ProcTable[MAXPROC];
int diskPID[USLOSS_DISK_UNITS];

/* -- Lists --*/
p4ProcPtr SleepList = NULL;
diskReqPtr diskRequestList[USLOSS_DISK_UNITS];
int numTracksOnDisk[USLOSS_DISK_UNITS];

int termPID[USLOSS_TERM_UNITS];
int charInMboxID[USLOSS_TERM_UNITS];
int charOutMboxID[USLOSS_TERM_UNITS];
int termReadPID[USLOSS_TERM_UNITS];
int termWritePID[USLOSS_TERM_UNITS];
char readline[USLOSS_TERM_UNITS][MAXLINE];
int readBufferMBox[USLOSS_TERM_UNITS];





/*-------------Drivers-------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
static int	TermDriver(char *);
int diskReadHandler(int);
int diskWriteHandler(int);
//TODO is this right proto?
static int TermReader(char *);
static int TermWriter(char *);

/*-------------Proto------------------*/
void sleep1(USLOSS_Sysargs*);
int sleepReal(int);
int clockPID;

void diskRead(USLOSS_Sysargs*);
int diskReadReal(int, int, int, int, void *);
void diskWrite(USLOSS_Sysargs*);
int diskWriteReal(int, int, int, int, void *);

void insertDiskRequest(diskReqPtr);
void diskQueuePrinter(int);

void diskSize(USLOSS_Sysargs*);
int diskSizeReal(int, int *, int *, int *);

void termRead(USLOSS_Sysargs*);
void termWrite(USLOSS_Sysargs*);

void addProcessToProcTable(void);
void nullifyProcessEntry(void);

void setUserMode(void);


/* --------- Functions ----------- */
void start3(void) {
    char	name[128];
    char    buf[10];
	char    termbuf[10];
    int		i;
    int		pid;
    int		status;
    
    // Check kernel mode here.
    if (!(USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE)) {
        USLOSS_Console("start3(): called while in user mode. ");
        USLOSS_Console("Halting...\n");
        USLOSS_Halt(1);
    }

	// Initialize systemCallVec with appropriate system call functions
    systemCallVec[SYS_SLEEP] 		= sleep1;
    systemCallVec[SYS_DISKREAD] 	= diskRead;
    systemCallVec[SYS_DISKWRITE] 	= diskWrite;
    systemCallVec[SYS_DISKSIZE] 	= diskSize;
    systemCallVec[SYS_TERMREAD] 	= termRead;
    systemCallVec[SYS_TERMWRITE] 	= termWrite;
    
    // Initialize Phase4 Process Table
    for(int k = 0; k < MAXPROC; k++){
    	p4ProcTable[k].status 		= NONACTIVE;
    	p4ProcTable[k].pid 			= NONACTIVE;
    	p4ProcTable[k].semID 		= semcreateReal(0);
    	p4ProcTable[k].wakeTime		= NONACTIVE;
    	p4ProcTable[k].nextSleeping = NULL;
    }
    
    // Create clockDriver process and semaphore.
    clockSemID = semcreateReal(0);
    pid = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (pid < 0) {
		USLOSS_Console("start3(): Can't create clock driver\n");
		USLOSS_Halt(1);
    }
    
    // Add clock process to process table
    p4ProcTable[pid % MAXPROC].pid = pid;
    clockPID = pid;
    
    // Block the clock driver on its semaphore. Will wake up once an interrupt happens
    sempReal(clockSemID);

    // Create the disk driver processes.
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(buf, "%d", i);
        sprintf(name, "disk%d", i);
        pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);

        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
            USLOSS_Halt(1);
        }
        
        // Add the disk drivers to the procTable and store their PIDs.
        diskPID[i] = pid;
        p4ProcTable[pid % MAXPROC].pid = pid;
    }

    // Create Terminal Processes (Drivers, Readers, & Writers)
    for(i = 0; i < USLOSS_TERM_UNITS; i++){
        
        // Create Terminal Driver [i] Process
        sprintf(termbuf, "%d", i);
        sprintf(name, "termDriver%d", i);
       	pid = fork1(name, TermDriver, buf, USLOSS_MIN_STACK, 2);
     	
        if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermDriver %d\n", i);
           USLOSS_Halt(1);
        }
        // Add Terminal Driver [i] to the process table and store its PID.
     	termPID[i] = pid;
        p4ProcTable[pid % MAXPROC].pid = pid;
     	
        // Create Terminal Reader [i] Process
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termReader%d", i);
       	pid = fork1(name, TermReader, buf, USLOSS_MIN_STACK, 2);
     	if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermReader %d\n", i);
           USLOSS_Halt(1);
        }
        
        // Add Terminal Reader [i] to the process table and store its PID.
     	termReadPID[i] = pid;
        p4ProcTable[pid % MAXPROC].pid = pid;
     	
        // Create Terminal Writer [i] Process
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termWriter%d", i);
       	pid = fork1(name, TermWriter, buf, USLOSS_MIN_STACK, 2);
     	
     	if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermWriter %d\n", i);
           USLOSS_Halt(1);
        }
        
        // Add Terminal Writer [i] to the process table and store its PID.
     	termWritePID[i] = pid;
        p4ProcTable[pid % MAXPROC].pid = pid;
     	
     	// Create Mailboxes for each terminal.
            // CharInMBox - Transfers single chars from Driver Process to Reader Process
            // CharOutMBox - Transfers singler chars from Driver Process to Writer Process
            // ReadBufferMBox - Stores lines from Read Process as they are completed
		charInMboxID[i] = MboxCreate(0, sizeof(char));
		charOutMboxID[i] = MboxCreate(0, sizeof(char));
		readBufferMBox[i] = MboxCreate(10, 80 * sizeof(char));
     }

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters, as shown in provided_prototypes.h
     */
    
    // Create Start4 Process to perform user/test procedures
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers once the program run has completed.
     */
    
    // Zap the clock driver
    zap(clockPID);  // clock driver
    
    // Zap the disk drivers
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        semvReal(p4ProcTable[diskPID[i] % MAXPROC].semID);
        zap(diskPID[i]);
    }

    // FIXME: Zap the term devices
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        // FIXME: Zappity-zip-zap
    }
    
    // Quit the program run. All processes should terminate.
    quit(0);
}

////////////// FIXME: BLOCK COMMENT //////////////
static int ClockDriver (char *arg) {
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(clockSemID);
    if (USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
    	USLOSS_Console("ERROR: ClockDriver(): Failed to change to User Mode.\n");
    }
	
	
    // Infinite loop until we are zap'd
    while(! isZapped()) {
        
        // Wait for an interrupt from the clock device.
        result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        if (result != 0) {
            return 0;
        }
        
        // Fetch the current USLOSS sysytem time
	 	if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: clockDriver(): Encountered error fetching current time. Halting.\n");
            USLOSS_Halt(1);
        }
	 	
        // Wake up any processes whose time has come.
	 	while(SleepList != NULL && SleepList->wakeTime <= status){
	 		int sleepSemID = SleepList->semID;
	 		SleepList = SleepList->nextSleeping;
	 		// TODO check return here?
            // Unblock the provess by V-ing the process' private semaphore.
	 		semvReal(sleepSemID);
	 	}
    }
    
    // Once the process is zapped, terminate.
    quit(1);
    return 0;
}

////////////// FIXME: BLOCK COMMENT //////////////
static int DiskDriver(char *arg) {
    int unit = atoi(arg);
    int diskResult;
    
    // --- FIXME: Query the disk size
    
    // Loop indefinitely until the process is zapped.
     while (!isZapped()) {
         
         // Block the DiskDirver Process until a new request is put on its queue.
         // Or the process is being terminated by start3()
         sempReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);
        
         // If there is no new request, then continue to see if it has been zapped
         if(diskRequestList[unit] == NULL) continue;
         
         // If there is a new request, deteremine the type and dispatch the proper handler
         switch (diskRequestList[unit]->requestType) {
                 
                 // If it is a Read Request, dispatch the read handler
             case USLOSS_DISK_READ:
               diskResult = diskReadHandler(unit);
               break;
                 
                 // If it is a Write Request, dispatch the write handler
             case USLOSS_DISK_WRITE:
               diskResult = diskWriteHandler(unit);
               break;
                 
                // Required by C, default should never be processed.
             default:
                 USLOSS_Console("default case in DiskDriver\n");
                 break;
         }
     }
    // When process is zapped, terminate.
     quit(0);
 }

////////////// FIXME: BLOCK COMMENT //////////////
int diskReadHandler(int unit) {

    int status;
    char sectorReadBuffer[512];
    
    // Grab the first request on the disk queue. Leave it on the queue.
    diskReqPtr currReq = diskRequestList[unit];
    
    // Grab the information about the read request
    int currTrack = currReq->startTrack;
    int currSector = currReq->startSector;
    int numSectors = currReq->numSectors;
    
    // Create a seek request
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_SEEK;
    devReq.reg1 = (void *)(long) currTrack;
    
    // Seek to specified first track for reading
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
    }
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
        currReq->status = status;
        return -1;
    }
    
    // Loop over each sector, reading at each.
    for (int i = 0; i < numSectors; i++) {
        
        // If we have read past the final sector on the current track, seek to the next track before reading.
        if (currSector >= USLOSS_DISK_TRACK_SIZE) {
            currTrack++;
            currSector = 0;
            
            // Create a seek request
            devReq.opr = USLOSS_DISK_SEEK;
            devReq.reg1 = (void *)(long) currTrack;
            
            // Seek to next track for reading
            if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
                USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
            }
            if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
                currReq->status = status;
                return -1;
            }
        }
        
        // Create a read request
        devReq.opr = USLOSS_DISK_READ;
        devReq.reg1 = ((void *) (long) currSector);
        devReq.reg2 = sectorReadBuffer;
        
        // Read from the current sector into the sectorReadBuffer
        if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
        }
        if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
            currReq->status = status;
            return -1;
        }
        
        // Copy what was read into the sectorReadBuffer to users buffer
        memcpy(((char *) currReq->buffer) + (i * 512), sectorReadBuffer, 512);
        // Move to the next sector
        currSector++;
    } // End For-loop
    
    // Save the most recent status as the requests return status
    currReq->status = status;
    
    // Remove the request from the queue and unblock the request's process.
    diskRequestList[unit] = diskRequestList[unit]->next;
    semvReal(currReq->semID);
    return 0;
}

////////////// FIXME: BLOCK COMMENT //////////////
int diskWriteHandler(int unit) {

    int status;
    
    // Grab the first request on the disk queue. Leave it on the queue.
    diskReqPtr currReq = diskRequestList[unit];
    
    // Grab the information about the write request
    int currTrack = currReq->startTrack;
    int currSector = currReq->startSector;
    int numSectors = currReq->numSectors;
    
    // Create a seek request
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_SEEK;
    devReq.reg1 = (void *)(long)currTrack;
    
    // Seek to specified first track for writing
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
    }
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
		currReq->status = status;
		return -1;
	}

    // Loop over each sector, writing at each.
    for (int i = 0; i < numSectors; i++) {
        
        // If we have written past the final sector on the current track, seek to the next track before writing.
        if (currSector >= USLOSS_DISK_TRACK_SIZE) {
            currTrack++;
            currSector = 0;
            
            // Create a seek request
            devReq.opr = USLOSS_DISK_SEEK;
			devReq.reg1 = (void *)(long)currTrack;
            
            // Seek to next track for writing
			if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
				USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
			}
			if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
				currReq->status = status;
				return -1;
			}
        }
        
        // Create a write request
        devReq.opr = USLOSS_DISK_WRITE;
		devReq.reg1 = (void *)(long)currSector;
		devReq.reg2 = currReq->buffer + i * 512;
	
        // Write to the current sector from the current location in the user's buffer
		if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
			USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
		}
		if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
			currReq->status = status;
			return -1;
		}
        // Move to the next sector
        currSector++;
    }
    
    // Save the most recent status as the requests return status
    currReq->status = status;
    
    // Remove the request from the queue and unblock the request's process.
    diskRequestList[unit] = diskRequestList[unit]->next;
    semvReal(currReq->semID);
    return 0;
}
/*
 *  These are the status codes returned by USLOSS_DeviceInput(). In general, 
 *  the status code is in the lower byte of the int returned; the upper
 *  bytes may contain other info. See the documentation for the
 *  specific device for details.
 */
// #define USLOSS_DEV_READY	0
// #define USLOSS_DEV_BUSY		1
// #define USLOSS_DEV_ERROR	2

/* 
 * USLOSS_DeviceOutput() and USLOSS_DeviceInput() will return DEV_OK if their 
 * arguments were valid and the device is ready, DEV_BUSY if the arguments were valid
 * but the device is busy, and DEV_INVALID otherwise. By valid, the device 
 * type and unit must correspond to a device that exists. 
 */

// #define USLOSS_DEV_OK		USLOSS_DEV_READY
// #define USLOSS_DEV_INVALID	USLOSS_DEV_ERROR

/*
 * These are the fields of the terminal status registers. A call to
 * USLOSS_DeviceInput will return the status register, and you can use these
 * macros to extract the fields. The xmit and recv fields contain the
 * status codes listed above.
 */

// #define USLOSS_TERM_STAT_CHAR(status)\
// 	(((status) >> 8) & 0xff)	/* character received, if any */

// #define	USLOSS_TERM_STAT_XMIT(status)\
// 	(((status) >> 2) & 0x3) 	/* xmit status for unit */

// #define	USLOSS_TERM_STAT_RECV(status)\
// 	((status) & 0x3)		/* recv status for unit */

/*
 * These are the fields of the terminal control registers. You can use
 * these macros to put together a control word to write to the
 * control registers via USLOSS_DeviceOutput.
 */

// #define USLOSS_TERM_CTRL_CHAR(ctrl, ch)\
// 	((ctrl) | (((ch) & 0xff) << 8))/* char to send, if any */
// 
// #define	USLOSS_TERM_CTRL_XMIT_INT(ctrl)\
// 	((ctrl) | 0x4)			/* enable xmit interrupts */
// 
// #define	USLOSS_TERM_CTRL_RECV_INT(ctrl)\
// 	((ctrl) | 0x2)			/* enable recv interrupts */
// 
// #define USLOSS_TERM_CTRL_XMIT_CHAR(ctrl)\
// 	((ctrl) | 0x1)			/* xmit the char in the upper bits */

////////////// FIXME: BLOCK COMMENT //////////////
////////////// FIXME: IN-LINE COMMENTS //////////////
static int TermDriver(char *arg){
	
	int unit = atoi(arg);
    short ctrl = 0;
    
    USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
	
	int wdResult, status;
    
    // Loop indefinitely, until Term Driver is zapped
	while(!isZapped()){
		wdResult = waitDevice(USLOSS_TERM_DEV,unit,&status);
		
		if (wdResult != 0) return 1;
        
        // If the current character is to be read from the Terminal, send it to the ReadDriver
		if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY){
			char toSend = USLOSS_TERM_STAT_CHAR(status);
			MboxSend(charInMboxID[unit], &toSend, sizeof(char));
		}
		 
		
		
	}
	
	return 0;
}

////////////// FIXME: BLOCK COMMENT //////////////
////////////// FIXME: IN-LINE COMMENTS //////////////
static int TermReader(char *arg){
	int unit = atoi(arg);
	
	char toBuild[80];
	char *input;
	int counter = 0;
	int result;
	while(!isZapped()){
		result = MboxReceive(charInMboxID[unit], input, sizeof(char));
		if(result == -1) return 0;
		
		if(input == '\n' || counter == MAXLINE - 1){
			toBuild[counter++] = '\n';
		 	MboxCondSend(readBufferMBox[unit], (void *)toBuild, counter);
		 	counter = 0;
		}else{
			toBuild[counter++] = input;
		}
		// toBuild[counter++] = input;
// 		if(counter == MAXLINE || toBuild[counter] == '\n'){
// 		 	counter = 0;
// 		 	MBoxCondSend(readBufferMBox[unit], (void *)toBuild, 80);
// 		}
	}

}
////////////// FIXME: BLOCK COMMENT //////////////
////////////// FIXME: IN-LINE COMMENTS //////////////
static int TermWriter(char *arg){
	int unit = atoi(arg);
	int wdResult, status;
	while(!isZapped()){
		wdResult = waitDevice(USLOSS_TERM_DEV,unit,&status);
	}
	quit(0);
	return 1;
}


/*----------- USER AND KERNEL level functions ---------------- */
////////////// FIXME: BLOCK COMMENT //////////////
void sleep1(USLOSS_Sysargs *args){
    
    // Ensure we have actually received a sleep request
	if (args->number != SYS_SLEEP){
		terminateReal(1);
	}
	
    // Call sleep real and return to the user the retrun status.
	args->arg4 = (void *)(long)sleepReal((int)(long)args->arg1);
}

// Causes the calling process to become unrunnable for at least the specified number of seconds, 
// and not significantly longer. The seconds must be non-negative.
// Return values:
//		   -1: seconds is not valid
//			0: otherwise
////////////// FIXME: BLOCK COMMENT //////////////
int sleepReal(int seconds){
    
    // Ensure that the sleep request is valid.
	if (seconds < 0) return -1;
	
	int status;
	if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: sleepReal(): Encountered error fetching current time. Halting.\n");
        USLOSS_Halt(1);
    }
	
    // Add the request process to the process table
	int pid = getpid();
	p4ProcTable[pid % MAXPROC].status 	= SLEEP;
	p4ProcTable[pid % MAXPROC].pid 		= pid;
	p4ProcTable[pid % MAXPROC].wakeTime = seconds * 1000000 + status;
	
	p4ProcPtr toAdd = &p4ProcTable[pid % MAXPROC];
	
    // Insert the new sleep request into the Clock Driver sleep queue
	p4ProcPtr curr;
	p4ProcPtr prev;
	
    // Traverse until the spot for insertion is found.
	for(prev = NULL, curr = SleepList;
		curr != NULL && toAdd->wakeTime > curr->wakeTime;
		prev = curr, curr = curr->nextSleeping){;}
	
    // If the list is empty, insert at head
	if(curr == NULL && prev == NULL){
		SleepList = toAdd;
	}
    // If we need to insert at the front of the list, insert at head
	else if (prev == NULL){
		toAdd->nextSleeping = curr;
		SleepList = toAdd;
	}
    // Otherwise, insert in order
	else{
		prev->nextSleeping = toAdd;
		toAdd->nextSleeping = curr;
	}
	
    // Block the current process until the Clock Driver wakes it up.
	sempReal(p4ProcTable[pid % MAXPROC].semID);
	
    // Nullify the entry in the process table after it has been processed.
	p4ProcTable[pid % MAXPROC].status 	= NONACTIVE;
	p4ProcTable[pid % MAXPROC].pid 		= NONACTIVE;
	p4ProcTable[pid % MAXPROC].wakeTime = NONACTIVE;
	
	return 0;
}





////////////////////////////////////////////////////////////////////////////////////////////////

////////////// FIXME: BLOCK COMMENT //////////////
void diskRead(USLOSS_Sysargs *args){
    
    // Extract the arguments from the user system call.
    void * buffer = args->arg1;
    int numSectors = (int)(long)args->arg2;
    int startTrack = (int)(long)args->arg3;
    int startSector = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    
    // Call diskReadReal to perform the actual insertion of the request
    int readResult = diskReadReal(unit, startTrack, startSector, numSectors, buffer);
    
    // If invalid arguments, store -1 in arg1, else store 0 in arg4
    if (readResult == -1) {
        args->arg4 = ((void *) (long) -1);
    } else {
        args->arg4 = ((void *) (long) 0);
    }
    
    // Store the result of the disk read in arg1
    args->arg1 = ((void *) (long) readResult);
}

// Reads sectors sectors from the disk indicated by unit, starting at track track and sector first. 
// The sectors are copied into buffer. Your driver must handle a range of sectors specified by first 
// and sectors that spans a track boundary 
// (after reading the last sector in a track it should read the first sector in the next track). 
// A file cannot wrap around the end of the disk.
// Return values:
// 			-1: invalid parameters
// 			 0: sectors were read successfully >0: disk’s status register
////////////// FIXME: BLOCK COMMENT //////////////
int diskReadReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // Error checking
    // FIXME: Requests go beyond available diskLocations on topEnd
    if (numSectors <= 0 || startTrack < 0 || startSector < 0) {
        return -1;
    }
    
    if (unit < 0 || unit > USLOSS_DISK_UNITS) {
        return -1;
    }
    
    // Add process to process table
    addProcessToProcTable();
    
    // Build diskRequest
    diskReqInfo newRequest;
    newRequest.status = EMPTY;
    newRequest.requestType = USLOSS_DISK_READ;
    newRequest.unit = unit;
    newRequest.buffer = buffer;
    newRequest.startSector = startSector;
    newRequest.startTrack = startTrack;
    newRequest.numSectors = numSectors;
    newRequest.semID = p4ProcTable[getpid() % MAXPROC].semID;
    newRequest.pid = getpid();
    newRequest.next = NULL;
    
    // Insert into disk request queue
    insertDiskRequest(&newRequest);
    
    // Wake up disk driver.
    semvReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);
    
    // Block process and wait for driver
    sempReal(newRequest.semID);
    
    // Remove from process table.
    nullifyProcessEntry();
    
    // Return status
	return newRequest.status;
}

////////////// FIXME: BLOCK COMMENT //////////////
void diskWrite(USLOSS_Sysargs *args){
    
    // Extract the arguments from the user system call.
    void * buffer = args->arg1;
    int numSectors = (int)(long)args->arg2;
    int startTrack = (int)(long)args->arg3;
    int startSector = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    
    // Call diskWriteReal to perform the actual insertion of the request.
    int writeResult = diskWriteReal(unit, startTrack, startSector, numSectors, buffer);
    
    // If invalid arguments, store -1 in arg1, else store 0 in arg4
    if (writeResult == -1) {
        args->arg4 = ((void *) (long) -1);
    } else {
        args->arg4 = ((void *) (long) 0);
    }
    
    // Store the result of the disk read in arg1
    args->arg1 = ((void *) (long) writeResult);
}

// Writes sectors sectors to the disk indicated by unit, starting at track track and sector first. 
// The contents of the sectors are read from buffer. 
// Like diskRead, your driver must handle a range of sectors specified by first and sectors that 
// spans a track boundary. A file cannot wrap around the end of the disk.
//
// Return values:
// 			-1: invalid parameters
// 			 0: sectors were written successfully >0: disk’s status register
////////////// FIXME: BLOCK COMMENT //////////////
int diskWriteReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // FIXME: Requests go beyond available diskLocations on topEnd
    if (numSectors <= 0 || startTrack < 0 || startSector < 0) {
        return -1;
    }
    
    if (unit < 0 || unit > USLOSS_DISK_UNITS) {
        return -1;
    }

    // Add process to process table
    addProcessToProcTable();
    
    // Build diskRequest
    diskReqInfo newRequest;
    newRequest.status = EMPTY;
    newRequest.requestType = USLOSS_DISK_WRITE;
    newRequest.unit = unit;
    newRequest.buffer = buffer;
    newRequest.startSector = startSector;
    newRequest.startTrack = startTrack;
    newRequest.numSectors = numSectors;
    newRequest.semID = p4ProcTable[getpid() % MAXPROC].semID;
    newRequest.pid = getpid();
    newRequest.next = NULL;
    
    // Insert into disk request queue
    insertDiskRequest(&newRequest);
    
    // Wake up disk driver.
    semvReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);

    // Block process and wait for driver
    sempReal(newRequest.semID);

    // Remove from process table.
    nullifyProcessEntry();
    
    // Return status
    return newRequest.status;
}

////////////// FIXME: BLOCK COMMENT //////////////
void insertDiskRequest(diskReqPtr newRequest) {
    int unit = newRequest->unit;
    
    // If the queue is empty, insert at head of list.
    if (diskRequestList[unit] == NULL) {
        diskRequestList[unit] = newRequest;
    }
    // Otherwise, insert in order.
    else {
        diskReqPtr follower = diskRequestList[unit];
        diskReqPtr leader = follower->next;
        
        // If the new request is to be inserted before the head returns to track0
        if (newRequest->startTrack > diskRequestList[unit]->startTrack) {
            // Traverse the list until leader has passed the spot to insert or has fallen off the end of the queue.
            while (leader != NULL &&
                   leader->startTrack < newRequest->startTrack &&
                   leader->startTrack > follower->startTrack) {
                leader = leader->next;
                follower = follower->next;
            }
            // Insert between follower and leader.
            follower->next = newRequest;
            newRequest->next = leader;
        }
        // Otherwise, traverse past the highest track request and continue tracking until the location is found
        else {
            while (leader != NULL && follower->startTrack <= leader->startTrack) {
                leader = leader->next;
                follower = follower->next;
            }
            while (leader != NULL && leader->startTrack <= newRequest->startTrack) {
                leader = leader->next;
                follower = follower->next;
            }
            // Insert between follower and leader.
            follower->next = newRequest;
            newRequest->next = leader;
        }
    }
}

////////////// FIXME: BLOCK COMMENT //////////////
void diskQueuePrinter(int unit) {
    diskReqPtr walker = diskRequestList[unit];
    
    printf("\n***********************************\n\n");
    
    while (walker != NULL) {
        printf("Request Type  = %d\n", walker->requestType);
        printf("Request Track = %d\n\n", walker->startTrack);
        walker = walker->next;
    }
    
    printf("***********************************\n\n");
}

////////////// FIXME: BLOCK COMMENT //////////////
void addProcessToProcTable() {
    
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = currPID;
    p4ProcTable[currPID % MAXPROC].status = ACTIVE;
    p4ProcTable[currPID % MAXPROC].semID = semcreateReal(0);
}

////////////// FIXME: BLOCK COMMENT //////////////
void nullifyProcessEntry() {
    
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = NONACTIVE;
    p4ProcTable[currPID % MAXPROC].status = EMPTY;
    p4ProcTable[currPID % MAXPROC].semID = -1;
}


////////////// FIXME: BLOCK COMMENT //////////////
void diskSize(USLOSS_Sysargs *args){
    
	if (args->number != SYS_DISKSIZE){
		terminateReal(1);
	}
    
    // Extract the arguments from the user system call.
	int unit = (int)(long)args->arg1;
	int sectorSize;
	int sectorsInTrack;
	int tracksInDisk;
    
    // Call diskSizeReal to determine the actual size of the called disk
	args->arg4 = (void *)(long) diskSizeReal(unit, &sectorSize, &sectorsInTrack, &tracksInDisk);

    // Return arguemnts to the user.
	args->arg1 = (void *)(long)sectorSize;
	args->arg2 = (void *)(long)sectorsInTrack;
	args->arg3 = (void *)(long)tracksInDisk;
	setUserMode();
}

// Returns information about the size of the disk indicated by unit. The sector parameter is filled in with the number of bytes in a sector, track with the number of sectors in a track, and disk with the number of tracks in the disk.
// Return values:
// 		-1: invalid parameters
// 		 0: disk size parameters returned successfully
////////////// FIXME: BLOCK COMMENT //////////////
int diskSizeReal(int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk){
	
    // Error check
    if (unit < 0 || unit > 1) {
        return -1;
    }
    
    addProcessToProcTable();

    // Build deviceRequest
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_TRACKS;
    devReq.reg1 = (void *)tracksInDisk;
    
    // Make call to DeviceInput. Number of tracks will be returned in tracksInDisk
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskSizeReal(): DeviceOutput failed");
    }
    
    int status;
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) != 0) {
        return -1;
    }
    if (status == USLOSS_DEV_ERROR) {
        return -1;
    }
    
    // Set number of sectors per track and size of each sector. Both are given constants
    *sectorSize = USLOSS_DISK_SECTOR_SIZE;
    *sectorsInTrack = USLOSS_DISK_TRACK_SIZE;
    
    nullifyProcessEntry();
    
	return 0;
}

////////////// FIXME: BLOCK COMMENT //////////////
////////////// FIXME: IN-LINE COMMENTS //////////////
void termRead(USLOSS_Sysargs *args){
    
    // Extract the user arguemnts for the terminal read request
	char *buffer = args->arg2;
	int size = (int)(long)args->arg2;
	int unit = (int)(long)args->arg3;
	
    // Call termReadReal in order to
	int resultCharsRead = termReadReal(unit, size, buffer);
	
    // Determine return values for user functions
	if(resultCharsRead < 0 ){
		args->arg4 = (void *)(long)-1;
	}else{
		args->arg4 = (void *)(long)0;
	}
	args->arg2 = (void *)(long)resultCharsRead;
}

// This routine reads a line of text from the terminal indicated by unit into the buffer pointed to by buffer. A line of text is terminated by a newline character (‘\n’), which is copied into the buffer along with the other characters in the line. 
// If the length of a line of input is greater than the value of the size parameter, then the first size characters are returned and the rest discarded.
// The terminal device driver should maintain a fixed-size buffer of 10 lines to store characters read prior to an invocation of termRead (i.e. a read-ahead buffer). Characters should be discarded if the read-ahead buffer overflows.
// Return values:
// 			-1: invalid parameters
// 			>0: number of characters read
////////////// FIXME: BLOCK COMMENT //////////////
////////////// FIXME: IN_LINE COMMENTS //////////////
int termReadReal(int unit, int size, char *buffer){
	if (unit < 0 || unit > 3) return -1;
	if (size < 0) return -1;
	
	int result;
	
	char linebuf[MAXLINE];
	result = MboxCondReceive(readBufferMBox[unit], linebuf, MAXLINE);
	
	strncpy(buffer, linebuf, size);
	return result;
	
}

////////////// FIXME: BLOCK COMMENT //////////////
void termWrite(USLOSS_Sysargs *args){
    char *buffer = args->arg2;
    int size = (int)(long)args->arg2;
    int unit = (int)(long)args->arg3;
    
    int resultCharsWritten;
    
    resultCharsWritten = termWriteReal(unit, size, buffer);
    
    if(resultCharsWritten < 0 ){
        args->arg4 = (void *)(long)-1;
    }else{
        args->arg4 = (void *)(long)0;
    }
    args->arg2 = (void *)(long)resultCharsWritten;
}

// This routine writes size characters — a line of text pointed to by text to the terminal 
// indicated by unit. A newline is not automatically appended, so if one is needed it must 
// be included in the text to be written. This routine should not return until the text has 
// been written to the terminal.
// Return values:
// 			-1: invalid parameters
// 			>0: number of characters written
int termWriteReal(int unit, int size, char *text){
	return 0;
}

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
