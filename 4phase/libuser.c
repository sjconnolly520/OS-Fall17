// Sleep (syscall SYS_SLEEP)
//  Input: arg1: number of seconds to delay the process
// Output: arg4: -1 if illegal values are given as input; 0 otherwise.
int Sleep(int seconds){
	return 0;
}

// DiskRead (syscall SYS_DISKREAD)
// Input
// 		arg1: the memory address to which to transfer arg2: number of sectors to read
// 		arg3: the starting disk track number
// 		arg4: the starting disk sector number
// 		arg5: the unit number of the disk from which to read
//
// Output
// 		arg1: 0 if transfer was successful; the disk status register otherwise. 
// 		arg4: -1 if illegal values are given as input; 0 otherwise.
int DiskRead(void *diskBuffer, int unit, int track, int first, 
                       int sectors, int *status){
	return 0;                      
}

int DiskWrite(void *diskBuffer, int unit, int track, int first,
                       int sectors, int *status){
	return 0;
}

int DiskSize (int unit, int *sector, int *track, int *disk){
	return 0;
}

int TermRead (char *buffer, int bufferSize, int unitID,
                       int *numCharsRead){
	return 0;                     
}

int TermWrite(char *buffer, int bufferSize, int unitID,
                       int *numCharsRead){
	return 0;                      
}