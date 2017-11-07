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

// Sleep (syscall SYS_SLEEP)
//  Input: arg1: number of seconds to delay the process
// Output: arg4: -1 if illegal values are given as input; 0 otherwise.
int Sleep(int seconds){
	CHECKMODE;
	
	USLOSS_Sysargs args;
	args.number = SYS_SLEEP;
	args.arg1 = (void *)(long)seconds;
	
	USLOSS_Syscall(&args);
	
	return (int)(long)args.arg4;
}

// DiskRead (syscall SYS_DISKREAD)
// Input
// 		arg1: the memory address to which to transfer
//      arg2: number of sectors to read
// 		arg3: the starting disk track number
// 		arg4: the starting disk sector number
// 		arg5: the unit number of the disk from which to read
//
// Output
// 		arg1: 0 if transfer was successful; the disk status register otherwise. 
// 		arg4: -1 if illegal values are given as input; 0 otherwise.
int DiskRead(void *diskBuffer, int unit, int track, int first, 
                       int sectors, int *status){
    
    CHECKMODE;
    
    USLOSS_Sysargs args;
    args.number = SYS_DISKREAD;
    args.arg1 = (void *)diskBuffer;
    args.arg2 = (void *)(long)sectors;
    args.arg3 = (void *)(long)track;
    args.arg4 = (void *)(long)first;
    args.arg5 = (void *)(long)unit;
    
    USLOSS_Syscall(&args);
    
    *status = (int)(long)args.arg1;
    return (int)(long)args.arg4;
}

int DiskWrite(void *diskBuffer, int unit, int track, int first,
                       int sectors, int *status){
    CHECKMODE;
    
    USLOSS_Sysargs args;
    args.number = SYS_DISKWRITE;
    args.arg1 = (void *)diskBuffer;
    args.arg2 = (void *)(long)sectors;
    args.arg3 = (void *)(long)track;
    args.arg4 = (void *)(long)first;
    args.arg5 = (void *)(long)unit;
    
    USLOSS_Syscall(&args);
    
    *status = (int)(long)args.arg1;
    return (int)(long)args.arg4;
}

// DiskSize (syscall SYS_DISKSIZE)
// Returns information about the size of the disk (diskSize). 
// Input
// 		arg1: the unit number of the disk
// Output
// 		arg1: size of a sector, in bytes
//		arg2: number of sectors in a track
//      arg3: number of tracks in the disk
//      arg4: -1 if illegal values are given as input; 0 otherwise.
int DiskSize (int unit, int *sectorSize, int *sectorsInTrack, int *tracksInDisk){
	CHECKMODE
	
	USLOSS_Sysargs args;
	args.number = SYS_DISKSIZE;
	args.arg1 = (void *)(long)unit;
	USLOSS_Syscall(&args);

	*sectorSize 	= (int)(long)args.arg1;
	*sectorsInTrack = (int)(long)args.arg2;
	*tracksInDisk 	= (int)(long)args.arg3; 
	
	return (int)(long)args.arg4;
}

int TermRead (char *buffer, int bufferSize, int unitID, int *numCharsRead){
	CHECKMODE
	
	USLOSS_Sysargs args;
	args.number = SYS_TERMREAD;
	args.arg1 = (void *)buffer;
	args.arg2 = (void *)(long)bufferSize;
	args.arg3 = (void *)(long)unitID;
	USLOSS_Syscall(&args);

	*numCharsRead 	= (int)(long)args.arg2;
	return (int)(long)args.arg4;
}

int TermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead){
	CHECKMODE
	
	USLOSS_Sysargs args;
	args.number = SYS_TERMWRITE;
	args.arg1 = (void *)buffer;
	args.arg2 = (void *)(long)bufferSize;
	args.arg3 = (void *)(long)unitID;
	USLOSS_Syscall(&args);

	*numCharsRead 	= (int)(long)args.arg2;
	return (int)(long)args.arg4;
}
