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

/* ------sems for the drivers etc ---- */
int 	running;

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
int writeBufferMBox[USLOSS_TERM_UNITS];
int charsWrittenMBox[USLOSS_TERM_UNITS];

/*-------------Drivers-------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
static int	TermDriver(char *);
int diskReadHandler(int);
int diskWriteHandler(int);
static int TermReader(char *);
static int TermWriter(char *);

/*-------------Proto------------------*/
void sleep1(USLOSS_Sysargs*);
int sleepReal(int);

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

int termReadReal(int, int, char *);
int termWriteReal(int, int, char*);


/* --------- Functions --------------*/
/* ------------------------------------------------------------------------
 Name - start3
 Purpose     - Initialized all of the required global variables for a function run.
             - Spawns a process called 'start4'
 Parameters  - void : may be usless, I'm not certain
 Returns     - 0 - This indicates something went wrong.
             - nothing - quit has been called successfully
 Side Effects - ProcTable is initialized, SemTable is initialized, sysCallVec is initialized
 ----------------------------------------------------------------------- */
void start3(void) {
    char	name[128];
    char    buf[10];
	char    termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    numTracksOnDisk[0] = -1;
    numTracksOnDisk[1] = -1;
    
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

   for (i = 0; i < USLOSS_DISK_UNITS; i++) {
       sprintf(buf, "%d", i);
       sprintf(name, "disk%d", i);
       pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);

       if (pid < 0) {
           USLOSS_Console("start3(): Can't create disk driver %d\n", i);
           USLOSS_Halt(1);
       }

       diskPID[i] = pid;
       //need more?
       // get disk size
       p4ProcTable[pid % MAXPROC].pid = pid;
       
	   int sectorSize; 
	   int sectorsInTrack;
	   diskSizeReal(i, &sectorSize, &sectorsInTrack, &numTracksOnDisk[i]);  
   }

    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */
     for(i = 0; i < USLOSS_TERM_UNITS; i++){
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termDriver%d", i);
       	pid = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
     	if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermDriver %d\n", i);
           USLOSS_Halt(1);
        }
     	termPID[i] = pid;
     	
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termReader%d", i);
       	pid = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
     	if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermReader %d\n", i);
           USLOSS_Halt(1);
        }
     	termReadPID[i] = pid;
     	
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termWriter%d", i);
       	pid = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
     	
     	if (pid < 0) {
           USLOSS_Console("start3(): Can't create TermWriter %d\n", i);
           USLOSS_Halt(1);
        }
     	termWritePID[i] = pid;
     	
     	
		charInMboxID[i] = MboxCreate(0, sizeof(char));
		charOutMboxID[i] = MboxCreate(0, sizeof(char));
		readBufferMBox[i] = MboxCreate(10, (MAXLINE + 1) * sizeof(char));
		writeBufferMBox[i] = MboxCreate(0, MAXLINE * sizeof(char));
		charsWrittenMBox[i] = MboxCreate(0, sizeof(int));
		
     }

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

	//disks
	semvReal(p4ProcTable[diskPID[0] % MAXPROC].semID);
    zap(diskPID[0]);

    semvReal(p4ProcTable[diskPID[1] % MAXPROC].semID);
	zap(diskPID[1]);

	
	//terms
	for(i = 0; i < USLOSS_TERM_UNITS; i++){
		MboxCondSend(charInMboxID[i], 0, 0);
		zap(termReadPID[i]);
		
		MboxCondSend(charOutMboxID[i], 0, 0);
		MboxCondSend(writeBufferMBox[i], "kill", 5);
		zap(termWritePID[i]);
		
	}
	char killFileName[50];
	FILE *killFile;
	for(i = 0; i < USLOSS_TERM_UNITS; i++){
		sprintf(killFileName, "term%d.in", i);
		killFile = fopen(killFileName, "a");
		fprintf(killFile, "Please... Kill... Me...");
		fclose(killFile);
		zap(termPID[i]);
	}
    // eventually, at the end:
    quit(0);
    
}

/* ------------------------------------------------------------------------
 Name - ClockDriver
 Purpose     - used by Sleep user mode process.
 Parameters  - char *arg - parameters from start3. Does nothing here
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
static int
ClockDriver(char *arg)
{
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    if (USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
    	USLOSS_Console("ERROR: ClockDriver(): Failed to change to User Mode.\n");
    }
	
	
    // Infinite loop until we are zap'd
    while(!isZapped()) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        if (isZapped()) {
            continue;
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
	 		semvReal(sleepSemID);
	 	}
    }
    
    quit(1);
    return 0;
}

/* ------------------------------------------------------------------------
 Name - DiskDriver
 Purpose      - used by DiskWrite and DiskRead user mode processes.
 Parameters   - char *arg - parameters from start3. indicates the unit# of the disk
 Returns      - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
static int
DiskDriver(char *arg)
{
    int unit = atoi(arg);

     while (!isZapped()) {
         sempReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);
         
         if(diskRequestList[unit] == NULL) continue;
         
         switch (diskRequestList[unit]->requestType) {
             case USLOSS_DISK_READ:
                 diskReadHandler(unit);
                 break;
                 
             case USLOSS_DISK_WRITE:
                 diskWriteHandler(unit);
                 break;
                 
             default:
                 USLOSS_Console("default case in DiskDriver\n");
                 break;
         }
     }
     quit(0);
     return 0;
 }

/* ------------------------------------------------------------------------
 Name - diskReadHandler
 Purpose      - Processes disk read requests from the user mode DiskRead
 Parameters   - int unit.  indicates the unit# of the disk
 Returns      - 0, -1 to indicate an error occurred
 Side Effects - modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskReadHandler(int unit) {

    int status;
    char sectorReadBuffer[512];
    
    diskReqPtr currReq = diskRequestList[unit];
    
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
    diskRequestList[unit] = diskRequestList[unit]->next;
    semvReal(currReq->semID);
    return 0;
}

/* ------------------------------------------------------------------------
 Name - diskWriteHandler
 Purpose      - Processes disk write requests from the user mode DiskWrite
 Parameters   - int unit.  indicates the unit# of the disk
 Returns      - 0, -1 to indicate an error occurred
 Side Effects - modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskWriteHandler(int unit) {

    int status;
    // Grab the current request from the front of the queue, remove it.
    diskReqPtr currReq = diskRequestList[unit];
    
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
        
        currSector++;
    }
    
    currReq->status = status;
    diskRequestList[unit] = diskRequestList[unit]->next;
    semvReal(currReq->semID);
    return 0;
}

/* ------------------------------------------------------------------------
 Name - TermDriver
 Purpose      - Processes terminal read and write requests requests from the user mode 
 				TermRead and TermWrite
 Parameters   - char *arg.  indicates the unit# of this terminal driver 
 Returns      - 0
 Side Effects - none
 ----------------------------------------------------------------------- */
static int TermDriver(char *arg){
	int unit = atoi(arg);
	
	int ctrl = 0;
	ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
	if ( USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl)  ==  USLOSS_DEV_INVALID){
		USLOSS_Console("termDriver(%d):  returned an error setting rcv int. Halting\n");
		USLOSS_Halt(1); 
	} 
	
	char toSend;
	int status;
	while(!isZapped()){
		waitDevice(USLOSS_TERM_DEV,unit,&status);
		
		if (isZapped()){
			continue;
		}
		
		if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY){
			toSend = USLOSS_TERM_STAT_CHAR(status);
			MboxCondSend(charInMboxID[unit], &toSend, sizeof(char));
		}
		if(USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY){
			MboxCondSend(charOutMboxID[unit], NULL, 0);
		}
		

	}
	
	quit(0);
	return 0;
}

/* ------------------------------------------------------------------------
 Name - TermReader
 Purpose      - Processes terminal read requests from the user mode TermRead
 Parameters   - char *arg.  indicates the unit# of the terminal driver this is 
 				associated with
 Returns      - 0
 Side Effects - none
 ----------------------------------------------------------------------- */
static int TermReader(char *arg){
	int unit = atoi(arg);
	
	char toBuild[MAXLINE + 1];
	char input;
	int counter = 0;
	while(!isZapped()){
		MboxReceive(charInMboxID[unit], &input, sizeof(char));
		if(isZapped()){
			continue;
		} 
		
		toBuild[counter++] = input;
		
		if(input == '\n' || counter == MAXLINE){
			toBuild[counter] = '\0';
		 	MboxCondSend(readBufferMBox[unit], (void *)toBuild, counter + 1);
		 	counter = 0;
		 }
		
	}
	quit(1);
	return 0;
}

/* ------------------------------------------------------------------------
 Name - TermWriter
 Purpose      - Processes terminal write requests from the user mode TermWrite
 Parameters   - char *arg.  indicates the unit# of the terminal driver this driver is
 				associated with
 Returns      - 0
 Side Effects - none
 ----------------------------------------------------------------------- */
static int TermWriter(char *arg){
	int unit = atoi(arg);
    int numBytesRcvd;
    int ctrl = 0;
    char toWrite[MAXLINE];
	while(!isZapped()){
		numBytesRcvd = MboxReceive(writeBufferMBox[unit], toWrite, MAXLINE);
		if (isZapped()){
			continue;	
		}

		for(int i = 0; i < numBytesRcvd; i++){
			ctrl = 0;
			ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, toWrite[i]);
			ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
			ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
			ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
			
			if(USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl) == USLOSS_DEV_INVALID){
				USLOSS_Console("USLOSS_DEV_INVALID xmit error unit %d",unit);	
			}
			
			MboxReceive(charOutMboxID[unit],0,0);
		}
		ctrl = 0;
		ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
		if(USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl) == USLOSS_DEV_INVALID){
			USLOSS_Console("USLOSS_DEV_INVALID xmit OFF error unit %d",unit);	
		}
		MboxSend(charsWrittenMBox[unit], &numBytesRcvd, sizeof(int));
	}
	
	quit(0);
	return 0;
}


/*----------- USER AND KERNEL level functions ---------------- */

/* ------------------------------------------------------------------------
 Name - sleep1
 Purpose      - helper function for sleepReal and Sleep user mode process. Error checking for SYS_SLEEP
 Parameters   - USLOSS_Sysargs *args
 Returns      - void
 Side Effects - none
 ----------------------------------------------------------------------- */
void sleep1(USLOSS_Sysargs *args){
	if (args->number != SYS_SLEEP){
		terminateReal(1);
	}
	
	args->arg4 = (void *)(long)sleepReal((int)(long)args->arg1);
}

/* ------------------------------------------------------------------------
 Name 			- sleepReal
 Purpose      	- Causes the calling process to become unrunnable for at least the 
 				  specified number of seconds
 Parameters   	- int seconds, to sleep for
 Returns      	- -1: seconds is not valid, 0: otherwise
 Side Effects 	- modifies SleepList
 ----------------------------------------------------------------------- */
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

/* ------------------------------------------------------------------------
 Name 			- diskRead
 Purpose      	- called by DiskRead user mode. calls diskReadReal
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- void
 Side Effects 	- none
 ----------------------------------------------------------------------- */
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

/* ------------------------------------------------------------------------
 Name 			- diskReadReal
 Purpose      	- Reads sectors sectors from the disk indicated by unit, starting at 
 				  track track and sector first. 
 Parameters   	- int unit, int startTrack, int startSector, int numSectors, void *buffer
 Returns      	- -1: invalid parameters or 
 				   0: sectors were read successfully or
 				  >0: disk’s status register
 Side Effects 	- modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskReadReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    
    if (numSectors <= 0                                             || 
        (startTrack < 0  || numTracksOnDisk[unit] - 1 < startTrack) || 
        (startSector < 0 || startSector > 15)) {
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

/* ------------------------------------------------------------------------
 Name 			- diskWrite
 Purpose      	- calls diskWriteReal 
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- void
 Side Effects 	- none
 ----------------------------------------------------------------------- */
void diskWrite(USLOSS_Sysargs *args){
    
    void * buffer = args->arg1;
    int numSectors = (int)(long)args->arg2;
    int startTrack = (int)(long)args->arg3;
    int startSector = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    
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

/* ------------------------------------------------------------------------
 Name 			- diskWriteReal
 Purpose      	- adds request to diskRequestList and wakes up disk
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- -1: invalid parameters or 
 				   0: sectors were written successfully or
 				  >0: disk’s status register
 Side Effects 	- modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskWriteReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    
    if (numSectors <= 0                                             || 
        (startTrack < 0  || numTracksOnDisk[unit] - 1 < startTrack) || 
        (startSector < 0 || startSector > 15)) {
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

/* ------------------------------------------------------------------------
 Name 			- insertDiskRequest
 Purpose      	- adds request to diskRequestList and wakes up disk
 Parameters   	- diskReqPtr newRequest
 Returns      	- void
 Side Effects 	- modifies the diskRequestList
 ----------------------------------------------------------------------- */
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

/* ------------------------------------------------------------------------
 Name 			- diskQueuePrinter
 Purpose      	- used for debug purposes
 Parameters   	- int unit
 Returns      	- void
 Side Effects 	- none
 ----------------------------------------------------------------------- */
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

/* ------------------------------------------------------------------------
 Name 			- addProcessToProcTable
 Purpose      	- add process to process table
 Parameters   	- int unit
 Returns      	- void
 Side Effects 	- modifies proctable
 ----------------------------------------------------------------------- */
void addProcessToProcTable() {
    
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = currPID;
    p4ProcTable[currPID % MAXPROC].status = ACTIVE;
    p4ProcTable[currPID % MAXPROC].semID = semcreateReal(0);
}

/* ------------------------------------------------------------------------
 Name 			- nullifyProcessEntry
 Purpose      	- clears process table entry
 Parameters   	- void
 Returns      	- void
 Side Effects 	- modifies proc table
 ----------------------------------------------------------------------- */
 void nullifyProcessEntry() {
    
    int currPID = getpid();
    p4ProcTable[currPID % MAXPROC].nextSleeping = NULL;
    p4ProcTable[currPID % MAXPROC].pid = NONACTIVE;
    p4ProcTable[currPID % MAXPROC].status = EMPTY;
    p4ProcTable[currPID % MAXPROC].semID = -1;
}

/* ------------------------------------------------------------------------
 Name 			- diskSize
 Purpose      	- calls diskSizeReal
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- void
 Side Effects 	- none
 ----------------------------------------------------------------------- */
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
	setUserMode();
}

/* ------------------------------------------------------------------------
 Name 			- diskSizeReal
 Purpose      	- Returns information about the size of the disk indicated by unit
 Parameters   	- int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk
 Returns      	- -1: invalid parameters or
 				   0: disk size parameters returned successfully
 Side Effects 	- none 
 ----------------------------------------------------------------------- */
int diskSizeReal(int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk){
	
    // Error check
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    
    if (numTracksOnDisk[unit] != -1){
    	*sectorSize = USLOSS_DISK_SECTOR_SIZE;
    	*sectorsInTrack = USLOSS_DISK_TRACK_SIZE;
    	*tracksInDisk = numTracksOnDisk[unit];
    	return 0;
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

/* ------------------------------------------------------------------------
 Name 			- termRead
 Purpose      	- helper for TermRead user mode. calls termReadReal
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- void
 Side Effects 	- none 
 ----------------------------------------------------------------------- */
void termRead(USLOSS_Sysargs *args){
	char *buffer = (char *)args->arg1;
	int size = (int)(long)args->arg2;
	int unit = (int)(long)args->arg3;
	
	int resultCharsRead;
	resultCharsRead = termReadReal(unit, size, buffer);
	
	if(resultCharsRead < 0 ){
		args->arg4 = (void *)(long)-1;
	}else{
		args->arg4 = (void *)(long)0;
	}
	args->arg2 = (void *)(long)resultCharsRead;
}

/* ------------------------------------------------------------------------
 Name 			- termReadReal
 Purpose      	- This routine reads a line of text from the terminal indicated by unit into the buffer
 Parameters   	- int unit, int size, char *buffer
 Returns      	- -1: invalid parameters or
 				  >0: number of characters read
 Side Effects 	- none 
 ----------------------------------------------------------------------- */
int termReadReal(int unit, int size, char *buffer){
	if (unit < 0 || unit > 3) return -1;
	if (size <= 0) return -1;
	
	int result;
	
	char linebuf[MAXLINE + 1];
	result = MboxReceive(readBufferMBox[unit], linebuf, MAXLINE + 1);
	if(result < 0){
		USLOSS_Console("termReadReal() received %d. Returning...\n", result);
		return result;
	}
	int linelength = strlen(linebuf);
	linebuf[linelength] = '\n';
	
	if (linelength > size){
		memcpy(buffer, linebuf, linelength + 1);
		return size;
	}else{
		memcpy(buffer, linebuf, linelength);
		return linelength;
	}
	
	return linelength;
}

/* ------------------------------------------------------------------------
 Name 			- termWrite
 Purpose      	- called by TermWrite. calls termWriteReal
 Parameters   	- USLOSS_Sysargs *args
 Returns      	- void
 Side Effects 	- none 
 ----------------------------------------------------------------------- */
void termWrite(USLOSS_Sysargs *args){
	char *text = (char *)args->arg1;
	int size = (int)(long)args->arg2;
	int unit = (int)(long)args->arg3;
	
	int resultCharsWritten;
	resultCharsWritten = termWriteReal(unit, size, text);
	
	if(resultCharsWritten < 0 ){
		args->arg4 = (void *)(long)-1;
	}else{
		args->arg4 = (void *)(long)0;
	}
	args->arg2 = (void *)(long)resultCharsWritten;
}

/* ------------------------------------------------------------------------
 Name 			- termWriteReal
 Purpose      	- This routine writes characters
 Parameters   	- int unit, int size, char *text
 Returns      	- number of characters written or
 				  -1 if invalid parameters
 Side Effects 	- none 
 ----------------------------------------------------------------------- */
int termWriteReal(int unit, int size, char *text){
	if (unit < 0 || unit > USLOSS_TERM_UNITS) return -1;
	
	if(size < 1 || size > MAXLINE){
		return -1;
	}
	
	MboxSend(writeBufferMBox[unit], text, size);
	int charsWritten;
	MboxReceive(charsWrittenMBox[unit], &charsWritten, sizeof(int));
	return charsWritten;
}

/* ------------------------------------------------------------------------
 Name - setUserMode
 Purpose     - Sets the PSR to UserMode, (Sets first bit to 0)
 Parameters  - none
 Returns     - nothing
 Side Effects - Sets PSR Mode Bit to 0
 ----------------------------------------------------------------------- */
void setUserMode() {
    if (USLOSS_PsrSet(USLOSS_PsrGet() & 0xE) == USLOSS_ERR_INVALID_PSR) {
        USLOSS_Console("ERROR: setUserMode(): Failed to change to User Mode.\n");
    }
}
