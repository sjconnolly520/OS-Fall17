#ifndef DRIVER_H

#define DRIVER_H

#define NONACTIVE 	-1
#define ACTIVE 		1
#define SLEEP		2
#define DISK		3
#define TERM		4

typedef struct p4Proc p4Proc;
typedef struct p3Proc *p4ProcPtr;

struct p4Proc{
    int             status;
    int             semID;
    int 			pid;
    int				wakeTime;
    
    p4ProcPtr       nextSleeping;
    
};

#endif DRIVER_H