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
int 	running;

/* ----------- Globals ------------- */
p4Proc p4ProcTable[MAXPROC];
int diskPID[USLOSS_DISK_UNITS];

/* -- Lists --*/
p4ProcPtr SleepList = NULL;
diskReqPtr diskRequestList[USLOSS_DISK_UNITS];
int numTracksOnDisk[USLOSS_DISK_UNITS];



/*-------------Drivers-------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
int diskReadHandler(int);
int diskWriteHandler(int);
//TODO is this right proto?
//static int	TermDriver(char *);

/*-------------Proto------------------*/
void sleep1(USLOSS_Sysargs*);
int sleepReal(int);

void diskRead(USLOSS_Sysargs*);
int diskReadReal(int, int, int, int, void *);
void diskWrite(USLOSS_Sysargs*);
int diskWriteReal(int, int, int, int, void *);

void insertDiskRequest(diskReqPtr);

void diskSize(USLOSS_Sysargs*);
int diskSizeReal(int, int *, int *, int *);

void termRead(USLOSS_Sysargs*);
void termWrite(USLOSS_Sysargs*);

void addProcessToProcTable(void);
void nullifyProcessEntry(void);


/* --------- Functions ----------- */
void start3(void) {
    char	name[128];
    char    buf[10];
//     char    termbuf[10];
    int		i;
    int		clockPID;
    int 	diskPID0;
    int 	diskPID1;
    int		pid;
    int		status;
    /*
     * Check kernel mode here.
     */

	// Initialize systemCallVec with appropriate system call functions
    systemCallVec[SYS_SLEEP] 		= sleep1;
    systemCallVec[SYS_DISKREAD] 	= diskRead;
    systemCallVec[SYS_DISKWRITE] 	= diskWrite;
    systemCallVec[SYS_DISKSIZE] 	= diskSize;
    systemCallVec[SYS_TERMREAD] 	= termRead;
    systemCallVec[SYS_TERMWRITE] 	= termWrite;
    
    /* init proc table */
    for(int k = 0; k < MAXPROC; k++){
    	p4ProcTable[k].status 		= NONACTIVE;
    	p4ProcTable[k].pid 			= NONACTIVE;
    	p4ProcTable[k].semID 		= semcreateReal(0);
    	p4ProcTable[k].wakeTime		= NONACTIVE;
    	
    	p4ProcTable[k].nextSleeping = NULL;
    }
    
    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreateReal(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
		USLOSS_Console("start3(): Can't create clock driver\n");
		USLOSS_Halt(1);
    }
    
    //set clockPID in proc table
    p4ProcTable[clockPID % MAXPROC].pid = clockPID;
    
    
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */

    sempReal(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */

//    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
//        sprintf(buf, "%d", i);
//        sprintf(name, "disk%d", i);
//        pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);
//
//        if (pid < 0) {
//            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
//            USLOSS_Halt(1);
//        }
//
//        diskPID[i] = pid;
//        //need more?
//        // get disk size
//        p4ProcTable[pid % MAXPROC].pid = pid;
//    }

    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters, as shown in provided_prototypes.h
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver
//    zap(diskPID0);  // disk0 driver
//    zap(diskPID1);  // disk1 driver
	
    // eventually, at the end:
    quit(0);
    
}

static int
ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    if (USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
    	USLOSS_Console("ERROR: ClockDriver(): Failed to change to User Mode.\n");
    }
	
	
    // Infinite loop until we are zap'd
    while(! isZapped()) {
        result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        if (result != 0) {
            return 0;
        }
	/*
	 * Compute the current time and wake up any processes
	 * whose time has come.
	 */
	 	if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: clockDriver(): Encountered error fetching current time. Halting.\n");
            USLOSS_Halt(1);
        }
	 	
	 	while(SleepList != NULL && SleepList->wakeTime <= status){
	 		int sleepSemID = SleepList->semID;
	 		SleepList = SleepList->nextSleeping;
	 		//TODO check return here?
	 		semvReal(sleepSemID);
	 	}
    }
    
    quit(1);
    return 0;
}

static int
DiskDriver(char *arg)
{
    int unit = atoi(arg);
    int diskResult;
    
    // --- Query the disk size
     
     while (!isZapped()) {
         sempReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);

         switch (diskRequestList[unit]->requestType) {
             case USLOSS_DISK_READ:
                 diskResult = diskReadHandler(unit);
                 break;
                 
             case USLOSS_DISK_WRITE:
                 diskResult = diskWriteHandle(unit);
                 break;
                 
             default:
                 terminateReal(1);
                 break;
         }
     }
 }


int diskReadHandler(int unit) {
    
    int status;
    
    char sectorReadBuffer[512];
    
    diskReqPtr currReq = diskRequestList[unit];
    diskRequestList[unit] = diskRequestList[unit]->next;
    
    int currTrack = currReq->startTrack;
    int currSector = currReq->startSector;
    int numSectors = currReq->numSectors;
    
    // Seek to specified track for writing
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_SEEK;
    devReq.reg1 = (void *)(long) currTrack;
    
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
    }
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
        currReq->status = status;
        return -1;
    }
    
    for (int i = 0; i < numSectors; i++) {
        
        // If we have written all sectors on the current track, seek to the next track before writing.
        if (currSector >= USLOSS_DISK_TRACK_SIZE) {
            currTrack++;
            currSector = 0;
            
            // Seek to specified track for writing
            devReq.opr = USLOSS_DISK_SEEK;
            devReq.reg1 = (void *)(long) currTrack;
            
            if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
                USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
            }
            if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
                currReq->status = status;
                return -1;
            }
        }
        
        devReq.opr = USLOSS_DISK_READ;
        devReq.reg1 = ((void *) (long) currSector);
        devReq.reg2 = sectorReadBuffer;
        
        if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: diskReadHandler(): May need to do something with this error.\n");
        }
        if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
            currReq->status = status;
            return -1;
        }
        
        // Copy what was read to users buffer
        memcpy(((char *) currReq->buffer) + (i * 512), sectorReadBuffer, 512);
        currSector++;
    } // For-loop
    
    currReq->status = status;
    semvReal(p4ProcTable[currReq->pid % MAXPROC].semID);
    return 0;
}

int diskWriteHandler(int unit) {
    
    int status;
    // Grab the current request from the front of the queue, remove it.
    diskReqPtr currReq = diskRequestList[unit];
    diskRequestList[unit] = diskRequestList[unit]->next;
    
    int currTrack = currReq->startTrack;
    int currSector = currReq->startSector;
    int numSectors = currReq->numSectors;
    
    // Seek to specified track for writing
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_SEEK;
    devReq.reg1 = (void *)(long)currTrack;
    
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
    }
    
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
		currReq->status = status;
		return -1;
	}
    
    // Loop until all required sectors have been written
    for (int i = 0; i < numSectors; i++) {
        
        // If we have written all sectors on the current track, seek to the next track before writing.
        if (currSector >= USLOSS_DISK_TRACK_SIZE) {
            currTrack++;
            currSector = 0;
            
            // Seek to specified track for writing
			devReq.opr = USLOSS_DISK_SEEK;
			devReq.reg1 = (void *)(long)currTrack;
	
			if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
				USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
			}
			if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
				currReq->status = status;
				return -1;
			}
            
        }
        
        devReq.opr = USLOSS_DISK_WRITE;
		devReq.reg1 = (void *)(long)currSector;
		devReq.reg2 = currReq->buffer + i * 512;
	
		if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
			USLOSS_Console("ERROR: diskWriteHandler(): May need to do something with this error.\n");
		}
		if (waitDevice(USLOSS_DISK_DEV, unit, &status) < 0 ){
			currReq->status = status;
			return -1;
		}
        currSector+=1;
    }
    currReq->status = status;
    semvReal(p4ProcTable[currReq->pid % MAXPROC].semID);
    return 0;
}

// static int TermDriver(char *arg){
// 	return 0;
// }

/*----------- USER AND KERNEL level functions ---------------- */

void sleep1(USLOSS_Sysargs *args){
	if (args->number != SYS_SLEEP){
		terminateReal(1);
	}
	
	args->arg4 = (void *)(long)sleepReal((int)(long)args->arg1);
}

// Causes the calling process to become unrunnable for at least the specified number of seconds, 
// and not significantly longer. The seconds must be non-negative.
// Return values:
//		   -1: seconds is not valid
//			0: otherwise
int sleepReal(int seconds){
	if (seconds < 0) return -1;
	
	int status;
	if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: sleepReal(): Encountered error fetching current time. Halting.\n");
        USLOSS_Halt(1);
    }
	
	int pid = getpid();
	p4ProcTable[pid % MAXPROC].status 	= SLEEP;
	p4ProcTable[pid % MAXPROC].pid 		= pid;
	p4ProcTable[pid % MAXPROC].wakeTime = seconds * 1000000 + status;
	
	p4ProcPtr toAdd = &p4ProcTable[pid % MAXPROC];
	
	p4ProcPtr curr;
	p4ProcPtr prev;
	
	for(prev = NULL, curr = SleepList;
		curr != NULL && toAdd->wakeTime > curr->wakeTime;
		prev = curr, curr = curr->nextSleeping){;}
	
	if(curr == NULL && prev == NULL){
		SleepList = toAdd;
	}
	else if (prev == NULL){
		toAdd->nextSleeping = curr;
		SleepList = toAdd;
	}
	else{
		prev->nextSleeping = toAdd;
		toAdd->nextSleeping = curr;
	}
	
	sempReal(p4ProcTable[pid % MAXPROC].semID);
	
	p4ProcTable[pid % MAXPROC].status 	= NONACTIVE;
	p4ProcTable[pid % MAXPROC].pid 		= NONACTIVE;
	p4ProcTable[pid % MAXPROC].wakeTime = NONACTIVE;
	
	return 0;
}





////////////////////////////////////////////////////////////////////////////////////////////////

////////////// FIXME: BLOCK COMMENT //////////////
void diskRead(USLOSS_Sysargs *args){
    
    void * buffer = args->arg1;
    int numSectors = (int)(long)args->arg2;
    int startTrack = (int)(long)args->arg3;
    int startSector = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    
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
int diskReadReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // FIXME: Requests go beyond available diskLocations on topEnd
    if (numSectors <= 0 || startTrack <= 0 || startSector <= 0) {
        return -1;
    }
    
    if (unit < 0 || unit > 1) {
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
    
    void * buffer = args->arg1;
    int numSectors = (int)(long)args->arg2;
    int startTrack = (int)(long)args->arg3;
    int startSector = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    
    int writeResult = diskReadReal(unit, startTrack, startSector, numSectors, buffer);
    
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
int diskWriteReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // FIXME: Requests go beyond available diskLocations on topEnd
    if (numSectors <= 0 || startTrack <= 0 || startSector <= 0) {
        return -1;
    }
    
    if (unit < 0 || unit > 1) {
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
void addProcessToProcTable() {
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = currPID;
    p4ProcTable[currPID % MAXPROC].status = ACTIVE; //FIXME: What status does a new process have?
    p4ProcTable[currPID % MAXPROC].semID = SemCreate(0, 0);  // FIXME: Dustin wont like this. He's probably right.
}

////////////// FIXME: BLOCK COMMENT //////////////
void nullifyProcessEntry() {
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = NONACTIVE;
    p4ProcTable[currPID % MAXPROC].status = EMPTY;
    p4ProcTable[currPID % MAXPROC].semID = -1;  // FIXME: Dustin wont like this. He's probably right.
}


////////////////////////////////////////////////////////////////////////////////////////////////








void diskSize(USLOSS_Sysargs *args){
	if (args->number != SYS_DISKSIZE){
		terminateReal(1);
	}
	
	int unit = (int)(long)args->arg1;
	int sectorSize;
	int sectorsInTrack;
	int tracksInDisk;
	args->arg4 = (void *)(long) diskSizeReal(unit, &sectorSize, &sectorsInTrack, &tracksInDisk);
	
	args->arg1 = (void *)(long)sectorSize;
	args->arg2 = (void *)(long)sectorsInTrack;
	args->arg3 = (void *)(long)tracksInDisk;
}

// Returns information about the size of the disk indicated by unit. The sector parameter is filled in with the number of bytes in a sector, track with the number of sectors in a track, and disk with the number of tracks in the disk.
// Return values:
// 		-1: invalid parameters
// 		 0: disk size parameters returned successfully
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
    
    // Make call to DeviceInput.
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
    
    *sectorSize = USLOSS_DISK_SECTOR_SIZE;
    *sectorsInTrack = USLOSS_DISK_TRACK_SIZE;
    
    nullifyProcessEntry();
    
	return 0;
}

void termRead(USLOSS_Sysargs *args){

}

// This routine reads a line of text from the terminal indicated by unit into the buffer pointed to by buffer. A line of text is terminated by a newline character (‘\n’), which is copied into the buffer along with the other characters in the line. If the length of a line of input is greater than the value of the size parameter, then the first size characters are returned and the rest discarded.
// The terminal device driver should maintain a fixed-size buffer of 10 lines to store characters read prior to an invocation of termRead (i.e. a read-ahead buffer). Characters should be discarded if the read-ahead buffer overflows.
// Return values:
// 			-1: invalid parameters
// 			>0: number of characters read
int termReadReal(int unit, int size, char *buffer){

	return 0;
}

void termWrite(USLOSS_Sysargs *args){

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
