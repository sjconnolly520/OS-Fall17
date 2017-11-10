/*
 * by Stephen Connolly and Dustin Janzen
 *
 * CS 452 - Fall 2017
 */

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

/*----------------- Globals --------------------*/
p4Proc      p4ProcTable[MAXPROC];
int         diskPID[USLOSS_DISK_UNITS];
int         clockSemID;

/*------------------ Lists ---------------------*/
p4ProcPtr   SleepList = NULL;
diskReqPtr  diskRequestList[USLOSS_DISK_UNITS];     // Pointers to the heads of DiskRequest Queues
int         numTracksOnDisk[USLOSS_DISK_UNITS];     // Holds the number of tracks on disk [unit]

int         termPID[USLOSS_TERM_UNITS];             // Holds the PIDs of the Terminal Driver Processes
int         termReadPID[USLOSS_TERM_UNITS];         // Holds the PIDs of the Terminal Reader Processes
int         termWritePID[USLOSS_TERM_UNITS];        // Holds the PIDs of the Terminal Writer Processes
int         charInMboxID[USLOSS_TERM_UNITS];        // Mailboxes for passing chars to Terminal Reader Processes
int         charOutMboxID[USLOSS_TERM_UNITS];       // Mailboxes for alerting Terminal Writer Processes they are ready to write
int         readBufferMBox[USLOSS_TERM_UNITS];      // Mailboxes for passing lines read by Terminal Reader Processes
int         writeBufferMBox[USLOSS_TERM_UNITS];     // Mailboxes for passing lines for the Terminal Writer Processes to write
int         charsWrittenMBox[USLOSS_TERM_UNITS];    // Mailboxes for passing number of chars written by Terminal Writer Processes

/*----------------- Prototypes ------------------*/
/*------------------- Drivers -------------------*/
static int	ClockDriver(char *);
static int	DiskDriver(char *);
static int	TermDriver(char *);
int         diskReadHandler(int);
int         diskWriteHandler(int);
static int  TermReader(char *);
static int  TermWriter(char *);

/*----------------- Supporters ------------------*/
void        sleep1(USLOSS_Sysargs*);
int         sleepReal(int);

void        diskSize(USLOSS_Sysargs*);
int         diskSizeReal(int, int *, int *, int *);

void        diskRead(USLOSS_Sysargs*);
int         diskReadReal(int, int, int, int, void *);
void        diskWrite(USLOSS_Sysargs*);
int         diskWriteReal(int, int, int, int, void *);

void        insertDiskRequest(diskReqPtr);
void        diskQueuePrinter(int);

void        termRead(USLOSS_Sysargs*);
int         termReadReal(int, int, char *);
void        termWrite(USLOSS_Sysargs*);
int         termWriteReal(int, int, char*);

void        addProcessToProcTable(void);
void        nullifyProcessEntry(void);

void        setUserMode(void);

/* --------------- Functions ----------------*/
/* ------------------------------------------------------------------------
 Name - start3
 Purpose     - Initialized all of the required global variables for a function run.
             - Spawns a process called 'start4'
 Parameters  - none
 Returns     - 0        - This indicates something went wrong.
             - nothing  - quit() has been called successfully
 Side Effects - ProcTable is initialized, SemTable is initialized, sysCallVec is initialized,
                All device handler processes are forked. All Device handler processes are zapped.
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
       
       // Initialize numTracksOnDisk array. Future calls to diskSize/diskSizeReal will return the stored value.
	   int sectorSize; 
	   int sectorsInTrack;
	   diskSizeReal(i, &sectorSize, &sectorsInTrack, &numTracksOnDisk[i]);  
   }

    // May be other stuff to do here before going on to terminal drivers

    // Create Terminal Processes (Drivers, Readers, & Writers)
    for(i = 0; i < USLOSS_TERM_UNITS; i++){
        
        // Create Terminal Driver [i] Process
     	sprintf(termbuf, "%d", i);
       	sprintf(name, "termDriver%d", i);
       	pid = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
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
       	pid = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
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
       	pid = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
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
            // ReadBufferMBox - Stores lines from Reader Process as they are completed
            // WriteBufferMBox - Stores lines for Writer Process to write
            // CharsWrittenMBox - Stores the number of characters a Writer Process wrote
		charInMboxID[i] = MboxCreate(0, sizeof(char));
		charOutMboxID[i] = MboxCreate(0, sizeof(char));
		readBufferMBox[i] = MboxCreate(10, (MAXLINE + 1) * sizeof(char));
		writeBufferMBox[i] = MboxCreate(0, MAXLINE * sizeof(char));
		charsWrittenMBox[i] = MboxCreate(0, sizeof(int));
		
    }
    
    // Create Start4 Process to perform user/test procedures
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers once the program run has completed.
     */
    
    // Zap the clock driver
    zap(clockPID);

    // Zap the disk drivers
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        semvReal(p4ProcTable[diskPID[i] % MAXPROC].semID);
        zap(diskPID[i]);
    }
	
	// Zap the Terminal Reader and Writer Processes
	for(i = 0; i < USLOSS_TERM_UNITS; i++){
        
        // Unblock Terminal Reader Process [i], zap it.
		MboxCondSend(charInMboxID[i], 0, 0);
		zap(termReadPID[i]);
		
        // Unblock Terminal Writer Process [i], zap it.
		MboxCondSend(charOutMboxID[i], 0, 0);
		MboxCondSend(writeBufferMBox[i], "kill", 5);
		zap(termWritePID[i]);
	}
    
    // Zap the Terminal Driver Processes
    char killFileName[50];
    FILE * killFile;
    for(i = 0; i < USLOSS_TERM_UNITS; i++){
        
        // Open the term[i].in file associated with the given Terminal Driver. Append some text.
        sprintf(killFileName, "term%d.in", i);
        killFile = fopen(killFileName, "a");
        fprintf(killFile, "Please... Kill... Me...");
        fclose(killFile);
        
        // Zap the Terminal Driver, when it wakes up, it will exit its loop.
        zap(termPID[i]);
    }
    
    // Quit the program run. All processes should terminate.
    quit(0);
}

/* ------------------------------------------------------------------------
 Name - ClockDriver
 Purpose     - used by Sleep user mode process.
 Parameters  - char *arg - parameters from start3. Does nothing here
 Returns     - nothing
 Side Effects - Sets PSR to User Mode
 ----------------------------------------------------------------------- */
static int ClockDriver(char *arg) {
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(clockSemID);
    if (USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
    	USLOSS_Console("ERROR: ClockDriver(): Failed to change to User Mode.\n");
    }
	
    // Infinite loop until we are zap'd
    while(!isZapped()) {
        
        // Wait for an interrupt from the clock device.
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        
        // If Clock Driver was zapped while waiting, exit the loop and quit().
        if (isZapped()) {
            continue;
        }
        
        // Fetch the current USLOSS system time
	 	if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: clockDriver(): Encountered error fetching current time. Halting.\n");
            USLOSS_Halt(1);
        }
	 	
        // Wake up any processes whose time has come.
	 	while(SleepList != NULL && SleepList->wakeTime <= status){
	 		int sleepSemID = SleepList->semID;
	 		SleepList = SleepList->nextSleeping;
            // Unblock the process by V-ing the process' private semaphore.
	 		semvReal(sleepSemID);
	 	}
    }
    
    // Once the process is zapped, terminate.
    quit(1);
    return 0;
}

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
 Name             - sleepReal
 Purpose          - Causes the calling process to become unrunnable for at least the
 specified number of seconds
 Parameters       - int seconds, to sleep for
 Returns          - -1: seconds is not valid, 0: otherwise
 Side Effects     - modifies SleepList
 ----------------------------------------------------------------------- */
int sleepReal(int seconds){
    
    // Error check
    if (seconds < 0) return -1;
    
    int status;
    
    // Fetch the current USLOSS system time
    if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: sleepReal(): Encountered error fetching current time. Halting.\n");
        USLOSS_Halt(1);
    }
    
    // Add the sleep request to the process table
    int pid = getpid();
    p4ProcTable[pid % MAXPROC].status     = SLEEP;
    p4ProcTable[pid % MAXPROC].pid         = pid;
    p4ProcTable[pid % MAXPROC].wakeTime = seconds * 1000000 + status;
    
    // Add the sleep request to the sleep queue.
    p4ProcPtr toAdd = &p4ProcTable[pid % MAXPROC];
    p4ProcPtr curr;
    p4ProcPtr prev;
    
    for(prev = NULL, curr = SleepList;
        curr != NULL && toAdd->wakeTime > curr->wakeTime;
        prev = curr, curr = curr->nextSleeping){;}
    
    // If the head of the list is null, insert at head of sleep queue
    if(curr == NULL && prev == NULL){
        SleepList = toAdd;
    }
    // If request is to be added at the head of the queue, insert at head of sleep queue
    else if (prev == NULL){
        toAdd->nextSleeping = curr;
        SleepList = toAdd;
    }
    // Otherwise, insert in order
    else{
        prev->nextSleeping = toAdd;
        toAdd->nextSleeping = curr;
    }
    
    // Block the new sleep request until it is ready to wake up
    sempReal(p4ProcTable[pid % MAXPROC].semID);
    
    // Remove the process from the process table
    p4ProcTable[pid % MAXPROC].status     = NONACTIVE;
    p4ProcTable[pid % MAXPROC].pid         = NONACTIVE;
    p4ProcTable[pid % MAXPROC].wakeTime = NONACTIVE;
    
    return 0;
}

/* ------------------------------------------------------------------------
 Name - DiskDriver
 Purpose      - used by DiskWrite and DiskRead user mode processes.
 Parameters   - char *arg - parameters from start3. indicates the unit# of the disk
 Returns      - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
static int DiskDriver(char *arg) {
    int unit = atoi(arg);

    // Loop indefinitely until the process is zapped.
    while (!isZapped()) {
        
        // Block the DiskDirver Process until a new request is put on its queue.
        // Or the process is being terminated by start3()
        sempReal(p4ProcTable[diskPID[unit] % MAXPROC].semID);
        
        // If there is no new request, then continue to see if it has been zapped
        if (diskRequestList[unit] == NULL) continue;
        
        // If there is a new request, deteremine the type and dispatch the proper handler
        switch (diskRequestList[unit]->requestType) {
                
                // If it is a Read Request, dispatch the read handler
            case USLOSS_DISK_READ:
                diskReadHandler(unit);
                break;
                
                // If it is a Write Request, dispatch the write handler
            case USLOSS_DISK_WRITE:
                diskWriteHandler(unit);
                break;
                
                // Required by C, default should never be processed.
            default:
                USLOSS_Console("Default case in DiskDriver.\n");
                break;
         }
     }
    
    // When process is zapped, terminate.
    quit(0);
    return 0;
 }

/* ------------------------------------------------------------------------
 Name             - diskSize
 Purpose          - calls diskSizeReal
 Parameters       - USLOSS_Sysargs *args
 Returns          - void
 Side Effects     - none
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
    
    // Return details about the disk to the user.
    args->arg1 = (void *)(long)sectorSize;
    args->arg2 = (void *)(long)sectorsInTrack;
    args->arg3 = (void *)(long)tracksInDisk;
    setUserMode();
}

/* ------------------------------------------------------------------------
 Name             - diskSizeReal
 Purpose          - Returns information about the size of the disk indicated by unit
 Parameters       - int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk
 Returns          - -1: invalid parameters or
 0: disk size parameters returned successfully
 Side Effects     - none
 ----------------------------------------------------------------------- */
int diskSizeReal(int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk){
    
    // Error check
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    
    // If the size of the disk has already been determined, return the stored values
    if (numTracksOnDisk[unit] != -1){
        *sectorSize = USLOSS_DISK_SECTOR_SIZE;
        *sectorsInTrack = USLOSS_DISK_TRACK_SIZE;
        *tracksInDisk = numTracksOnDisk[unit];
        return 0;
    }
    
    addProcessToProcTable();
    
    // Build disk size request
    USLOSS_DeviceRequest devReq;
    devReq.opr = USLOSS_DISK_TRACKS;
    devReq.reg1 = (void *)tracksInDisk;
    
    int status;
    
    // Make disk size request
    if (USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &devReq) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: diskSizeReal(): DeviceOutput failed");
    }
    // If an error occurred in fetching number of disk tracks, return -1
    if (waitDevice(USLOSS_DISK_DEV, unit, &status) != 0) {
        return -1;
    }
    if (status == USLOSS_DEV_ERROR) {
        return -1;
    }
    
    // Set size of each sector in bytes and number of sectors on track. Constants.
    *sectorSize = USLOSS_DISK_SECTOR_SIZE;
    *sectorsInTrack = USLOSS_DISK_TRACK_SIZE;
    
    nullifyProcessEntry();
    
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
    
    // Grab the first request on the disk queue. Leave it on the queue.
    diskReqPtr currReq = diskRequestList[unit];
    
    // Grab the information about the read request.
    int currTrack = currReq->startTrack;
    int currSector = currReq->startSector;
    int numSectors = currReq->numSectors;
    
    // Create a seek request to the first track to read from
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
    }
    
    // Save the most recent status as the requests return status
    currReq->status = status;
    
    // Remove the request from the queue and unblock the request's process.
    diskRequestList[unit] = diskRequestList[unit]->next;
    semvReal(currReq->semID);
    return 0;
}

/* ------------------------------------------------------------------------
 Name             - diskRead
 Purpose          - called by DiskRead user mode. calls diskReadReal
 Parameters       - USLOSS_Sysargs *args
 Returns          - void
 Side Effects     - none
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
 Name             - diskReadReal
 Purpose          - Reads sectors sectors from the disk indicated by unit, starting at
 track track and sector first.
 Parameters       - int unit, int startTrack, int startSector, int numSectors, void *buffer
 Returns          - -1: invalid parameters or
 0: sectors were read successfully or
 >0: disk’s status register
 Side Effects     - modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskReadReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // Error check
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    if  (numSectors   <= 0  ||
         (startTrack   <  0  || numTracksOnDisk[unit] - 1 < startTrack) ||
         (startSector  <  0  || startSector > 15)) {
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
 Name - diskWriteHandler
 Purpose      - Processes disk write requests from the user mode DiskWrite
 Parameters   - int unit.  indicates the unit# of the disk
 Returns      - 0, -1 to indicate an error occurred
 Side Effects - modifies the diskRequestList
 ----------------------------------------------------------------------- */
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

    // Loop until all required sectors have been written
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

/* ------------------------------------------------------------------------
 Name             - diskWrite
 Purpose          - calls diskWriteReal
 Parameters       - USLOSS_Sysargs *args
 Returns          - void
 Side Effects     - none
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
 Name             - diskWriteReal
 Purpose          - adds request to diskRequestList and wakes up disk
 Parameters       - USLOSS_Sysargs *args
 Returns          - -1: invalid parameters or
 0: sectors were written successfully or
 >0: disk’s status register
 Side Effects     - modifies the diskRequestList
 ----------------------------------------------------------------------- */
int diskWriteReal(int unit, int startTrack, int startSector, int numSectors, void *buffer){
    
    // Error check
    if (unit < 0 || unit > USLOSS_DISK_UNITS - 1) {
        return -1;
    }
    if  (numSectors  <=  0  ||
         (startTrack  <   0  || numTracksOnDisk[unit] - 1 < startTrack) ||
         (startSector <   0  || startSector > 15)) {
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
 Name             - insertDiskRequest
 Purpose          - adds request to diskRequestList and wakes up disk
 Parameters       - diskReqPtr newRequest
 Returns          - void
 Side Effects     - modifies the diskRequestList
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
 Name             - diskQueuePrinter
 Purpose          - used for debug purposes
 Parameters       - int unit
 Returns          - void
 Side Effects     - none
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
 Name - TermDriver
 Purpose      - Processes terminal read and write requests requests from the user mode 
 				TermRead and TermWrite
 Parameters   - char *arg.  indicates the unit# of this terminal driver 
 Returns      - 0
 Side Effects - none
 ----------------------------------------------------------------------- */
static int TermDriver(char *arg){
	int unit = atoi(arg);
	
    // Enable interrupts
	int ctrl = 0;
	ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
	if ( USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl)  ==  USLOSS_DEV_INVALID){
		USLOSS_Console("termDriver(%d):  returned an error setting rcv int. Halting\n");
		USLOSS_Halt(1); 
	} 
	
	char toSend;
	int status;
    
    // Loop indefinitely until process is zapped
	while(!isZapped()){
        
        // Block until receiving an interrupt
		waitDevice(USLOSS_TERM_DEV,unit,&status);
		
        // If process was zapped, terminate
		if (isZapped()){
			continue;
		}
		
        // If a char is to be read, send it to the Reader Process
		if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY){
			toSend = USLOSS_TERM_STAT_CHAR(status);
			MboxCondSend(charInMboxID[unit], &toSend, sizeof(char));
		}
        
        // If the writer process is ready, unblock it.
		if(USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY){
			MboxCondSend(charOutMboxID[unit], NULL, 0);
		}
	}
    
    // Terminate the process
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
	
    // Create an array in which to build and store the current line.
	char toBuild[MAXLINE + 1];
	char input;
	int counter = 0;
    
    // Loop indefinitely until process is zapped.
	while(!isZapped()){
        
        // Block until a char is passed to the TermReader by the TermDriver
		MboxReceive(charInMboxID[unit], &input, sizeof(char));
        
        // If the process was zapped while blocked, exit the loop.
		if(isZapped()){
			continue;
		} 
		
        // Append the new char to the line.
		toBuild[counter++] = input;
		
        // If we have reached the end of a line or the buffer is full, return it to the user.
		if(input == '\n' || counter == MAXLINE){
            
            // Append a null character, send the line to the user, and reset char counter.
			toBuild[counter] = '\0';
		 	MboxCondSend(readBufferMBox[unit], (void *)toBuild, counter + 1);
		 	counter = 0;
		 }
	}
    
    // Terminate the process
	quit(1);
	return 0;
}

/* ------------------------------------------------------------------------
 Name             - termRead
 Purpose          - helper for TermRead user mode. calls termReadReal
 Parameters       - USLOSS_Sysargs *args
 Returns          - void
 Side Effects     - none
 ----------------------------------------------------------------------- */
void termRead(USLOSS_Sysargs *args){
    char *buffer = (char *)args->arg1;
    int size = (int)(long)args->arg2;
    int unit = (int)(long)args->arg3;
    
    int resultCharsRead;
    resultCharsRead = termReadReal(unit, size, buffer);
    
    // If illegal arguments were given, return -1, 0 otherwise.
    if(resultCharsRead < 0 ){
        args->arg4 = (void *)(long)-1;
    }else{
        args->arg4 = (void *)(long)0;
    }
    // Return number of characters read.
    args->arg2 = (void *)(long)resultCharsRead;
}

/* ------------------------------------------------------------------------
 Name             - termReadReal
 Purpose          - This routine reads a line of text from the terminal indicated by unit into the buffer
 Parameters       - int unit, int size, char *buffer
 Returns          - -1: invalid parameters or
 >0: number of characters read
 Side Effects     - none
 ----------------------------------------------------------------------- */
int termReadReal(int unit, int size, char *buffer){
    
    // Error check
    if (unit < 0 || unit > USLOSS_TERM_UNITS - 1) return -1;
    if (size <= 0) return -1;
    
    int result;
    
    // Create array into which the terminal will read.
    char linebuf[MAXLINE + 1];
    
    // Block until the terminal has finished reading
    result = MboxReceive(readBufferMBox[unit], linebuf, MAXLINE + 1);
    
    // If the read returned less than 0 bytes read, an error occurred.
    if(result < 0){
        USLOSS_Console("termReadReal() received %d. Returning...\n", result);
        return result;
    }
    // Determine how many characters are in the string that was read.
    int linelength = strlen(linebuf);
    
    // Append a new line character
    linebuf[linelength] = '\n';
    
    // If the number of chars read is greater than the requested number of chars, return including a newline character
    if (linelength > size){
        memcpy(buffer, linebuf, linelength + 1);
        return size;
    }
    // Otherwise, return the number of chars requested
    else{
        memcpy(buffer, linebuf, linelength);
        return linelength;
    }
    
    // Should never reach here.
    return linelength;
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
    
    // Allocate space for writing a line to the terminal
    char toWrite[MAXLINE];
    
    // Loop indefinitely until the process is zapped
	while(!isZapped()){
        
        // Determine how many characters are to be written to the terminal
		numBytesRcvd = MboxReceive(writeBufferMBox[unit], toWrite, MAXLINE);
        
        // If the process was zapped while blocked, terminate.
		if (isZapped()){
			continue;	
		}

        // Loop, writing all requested characters
		for(int i = 0; i < numBytesRcvd; i++){
            
            // Build the int containing specifics about the char and containing the char
			ctrl = 0;
			ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, toWrite[i]);
			ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
			ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
			ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
			
            // Write the char
			if(USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl) == USLOSS_DEV_INVALID){
				USLOSS_Console("USLOSS_DEV_INVALID xmit error unit %d",unit);	
			}
			
            // Block until the next char is ready to be written
			MboxReceive(charOutMboxID[unit],0,0);
		}
        
        // Enable receive interrupts
		ctrl = 0;
		ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        
        // Notify the system that interrupts are enabled
		if(USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl) == USLOSS_DEV_INVALID){
			USLOSS_Console("USLOSS_DEV_INVALID xmit OFF error unit %d",unit);	
		}
        
        // Return to the user the number of chars that were written
		MboxSend(charsWrittenMBox[unit], &numBytesRcvd, sizeof(int));
	}
	
    // Terminate the process.
	quit(0);
	return 0;
}

/* ------------------------------------------------------------------------
 Name             - termWrite
 Purpose          - called by TermWrite. calls termWriteReal
 Parameters       - USLOSS_Sysargs *args
 Returns          - void
 Side Effects     - none
 ----------------------------------------------------------------------- */
void termWrite(USLOSS_Sysargs *args){
    char *text = (char *)args->arg1;
    int size = (int)(long)args->arg2;
    int unit = (int)(long)args->arg3;
    
    int resultCharsWritten;
    resultCharsWritten = termWriteReal(unit, size, text);
    
    // If illegal arguments were given, return -1, 0 otherwise.
    if(resultCharsWritten < 0 ){
        args->arg4 = (void *)(long)-1;
    }else{
        args->arg4 = (void *)(long)0;
    }
    // Return number of characters read.
    args->arg2 = (void *)(long)resultCharsWritten;
}

/* ------------------------------------------------------------------------
 Name             - termWriteReal
 Purpose          - This routine writes characters
 Parameters       - int unit, int size, char *text
 Returns          - number of characters written or
 -1 if invalid parameters
 Side Effects     - none
 ----------------------------------------------------------------------- */
int termWriteReal(int unit, int size, char *text){
    
    if (unit < 0 ||unit > USLOSS_TERM_UNITS - 1) return -1;
    if (size < 1 || size > MAXLINE) return -1;
    
    // Send the text to be written to the terminal to the Terminal Writer Process
    MboxSend(writeBufferMBox[unit], text, size);
    
    // Block until the Writer Process has finished writing.
    int charsWritten;
    MboxReceive(charsWrittenMBox[unit], &charsWritten, sizeof(int));
    
    // Return the number of chars that were written
    return charsWritten;
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
