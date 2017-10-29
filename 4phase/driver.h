#ifndef DRIVER_H
#define DRIVER_H

#define NONACTIVE 	-1
#define ACTIVE 		1
#define SLEEP		2
#define DISK		3
#define TERM		4

typedef struct p4Proc p4Proc;
typedef struct p4Proc *p4ProcPtr;
typedef struct diskReqInfo *diskReqPtr;
struct p4Proc{
    int             status;
    int             semID;
    int 			pid;
    int				wakeTime;
    
    p4ProcPtr       nextSleeping;
    
};

struct diskReqInfo{
	diskReqPtr				next;
	
	USLOSS_DeviceRequest	*request;
};

#endif