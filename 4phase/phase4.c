#include "usloss.h"
#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include <stdlib.h> /* needed for atoi() */

//sems for the drivers etc
semaphore 	running;

/* ----------- Globals ------------- */
p4Proc p4ProcTable[MAXPROC];


/* -- Lists --*/
p4ProcPtr SleepList = NULL;


/*-------------Drivers-------------- */
static int	ClockDriver(char *);
static int	DiskDriver(char *);
//TODO is this right proto?
static int	TermDriver(char *);

void
start3(void)
{
    char	name[128];
    char    termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;
    /*
     * Check kernel mode here.
     */

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
        pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }
    }

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
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! is_zapped()) {
        result = waitdevice(USLOSS_CLOCK_DEV, 0, &status);
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
}

static int
DiskDriver(char *arg)
{
    return 0;
}

//User AND Kernel level functions

static int TermDriver(char *arg){
	return 0;
}

int Sleep(int seconds){
	return 0;
}

// Causes the calling process to become unrunnable for at least the specified number of seconds, 
// and not significantly longer. The seconds must be non-negative.
// Return values:
//		   -1: seconds is not valid
//			0: otherwise
int sleepReal(int seconds){
	return 0;
}

int DiskRead(void *diskBuffer, int unit, int track, int first, 
                       int sectors, int *status){
                       
}

// Reads sectors sectors from the disk indicated by unit, starting at track track and sector first. 
// The sectors are copied into buffer. Your driver must handle a range of sectors specified by first 
// and sectors that spans a track boundary 
// (after reading the last sector in a track it should read the first sector in the next track). 
// A file cannot wrap around the end of the disk.
// Return values:
// 			-1: invalid parameters
// 			 0: sectors were read successfully >0: disk’s status register
int diskReadReal(int unit, int track, int first, int sectors,
                 void *buffer){
	return 0;           
}


int DiskWrite(void *diskBuffer, int unit, int track, int first,
                       int sectors, int *status){
	return 0;
}

// Writes sectors sectors to the disk indicated by unit, starting at track track and sector first. 
// The contents of the sectors are read from buffer. 
// Like diskRead, your driver must handle a range of sectors specified by first and sectors that 
// spans a track boundary. A file cannot wrap around the end of the disk.
//
// Return values:
// 			-1: invalid parameters
// 			 0: sectors were written successfully >0: disk’s status register
int diskWriteReal(int unit, int track, int first, int sectors,void *buffer){
	return 0;
}

int DiskSize (int unit, int *sector, int *track, int *disk){
	return 0;
}

// Returns information about the size of the disk indicated by unit. The sector parameter is filled in with the number of bytes in a sector, track with the number of sectors in a track, and disk with the number of tracks in the disk.
// Return values:
// 		-1: invalid parameters
// 		 0: disk size parameters returned successfully
int diskSizeReal(int unit, int *sector, int *track, int *disk){
	return 0;
}

int TermRead (char *buffer, int bufferSize, int unitID,
                       int *numCharsRead){
	return 0;                     
}

// This routine reads a line of text from the terminal indicated by unit into the buffer pointed to by buffer. A line of text is terminated by a newline character (‘\n’), which is copied into the buffer along with the other characters in the line. If the length of a line of input is greater than the value of the size parameter, then the first size characters are returned and the rest discarded.
// The terminal device driver should maintain a fixed-size buffer of 10 lines to store characters read prior to an invocation of termRead (i.e. a read-ahead buffer). Characters should be discarded if the read-ahead buffer overflows.
// Return values:
// 			-1: invalid parameters
// 			>0: number of characters read
int termReadReal(int unit, int size, char *buffer){

	return 0;
}


int TermWrite(char *buffer, int bufferSize, int unitID,
                       int *numCharsRead){
	return 0;                      
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
