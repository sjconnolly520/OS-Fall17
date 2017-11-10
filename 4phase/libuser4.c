/*
 * by Stephen Connolly and Dustin Janzen
 *
 * CS 452 - Fall 2017
 */

#include <usyscall.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <libuser.h>

#define CHECKMODE {    \
    if (USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) { \
        USLOSS_Console("Trying to invoke syscall from kernel\n"); \
        USLOSS_Halt(1);  \
    }  \
}

/*
 * User level call to create a sleep request
 */
int Sleep(int seconds){
    
	CHECKMODE;
	
    // Build a Sysargs
	USLOSS_Sysargs args;
	args.number = SYS_SLEEP;
	args.arg1 = (void *)(long)seconds;
	
    // Perform system call
	USLOSS_Syscall(&args);
	
    // Return results to user
	return (int)(long)args.arg4;
}

/*
 * User level call to create a disk read request
 */
int DiskRead(void *diskBuffer, int unit, int track, int first, int sectors, int *status){
    
    CHECKMODE;
    
    // Build a Sysargs
    USLOSS_Sysargs args;
    args.number = SYS_DISKREAD;
    args.arg1 = (void *)diskBuffer;
    args.arg2 = (void *)(long)sectors;
    args.arg3 = (void *)(long)track;
    args.arg4 = (void *)(long)first;
    args.arg5 = (void *)(long)unit;
    
    // Perform system call
    USLOSS_Syscall(&args);
    
    // Return results to user
    *status = (int)(long)args.arg1;
    return (int)(long)args.arg4;
}

/*
 * User level call to create a disk write request
 */
int DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status){
    
    CHECKMODE;
    
    // Build a Sysargs
    USLOSS_Sysargs args;
    args.number = SYS_DISKWRITE;
    args.arg1 = (void *)diskBuffer;
    args.arg2 = (void *)(long)sectors;
    args.arg3 = (void *)(long)track;
    args.arg4 = (void *)(long)first;
    args.arg5 = (void *)(long)unit;
    
    // Perform system call
    USLOSS_Syscall(&args);
    
    // Return results to user
    *status = (int)(long)args.arg1;
    return (int)(long)args.arg4;
}

/*
 * User level call to create a disk size request
 */
int DiskSize (int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk){
    
	CHECKMODE
	
    // Build a Sysargs
	USLOSS_Sysargs args;
	args.number = SYS_DISKSIZE;
	args.arg1 = (void *)(long)unit;
    
    // Perform system call
	USLOSS_Syscall(&args);
    
    // Return results to user
	*sectorSize 	= (int)(long)args.arg1;
	*sectorsInTrack = (int)(long)args.arg2;
	*tracksInDisk 	= (int)(long)args.arg3; 
	
	return (int)(long)args.arg4;
}

/*
 * User level call to create a terminal read request
 */
int TermRead (char *buffer, int bufferSize, int unitID, int *numCharsRead){
    
	CHECKMODE
	
    // Build a Sysargs
	USLOSS_Sysargs args;
	args.number = SYS_TERMREAD;
	args.arg1 = (void *)buffer;
	args.arg2 = (void *)(long)bufferSize;
	args.arg3 = (void *)(long)unitID;
    
    // Perform system call
	USLOSS_Syscall(&args);

    // Return results to user
	*numCharsRead 	= (int)(long)args.arg2;
	return (int)(long)args.arg4;
}

/*
 * User level call to create a terminal write request
 */
int TermWrite(char *buffer, int bufferSize, int unitID, int *numCharsWritten){
    
	CHECKMODE
	
    // Build a Sysargs
	USLOSS_Sysargs args;
	args.number = SYS_TERMWRITE;
	args.arg1 = (void *)buffer;
	args.arg2 = (void *)(long)bufferSize;
	args.arg3 = (void *)(long)unitID;
    
    // Perform system call
	USLOSS_Syscall(&args);

    // Return results to user
	*numCharsWritten = (int)(long)args.arg2;
	return (int)(long)args.arg4;
}
