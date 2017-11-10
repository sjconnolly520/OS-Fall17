/*
 * by Stephen Connolly and Dustin Janzen
 *
 * CS 452 - Fall 2017
 */


#ifndef DRIVER_H
#define DRIVER_H

#define NONACTIVE 	-1
#define EMPTY        0
#define ACTIVE 		 1
#define SLEEP		 2
#define DISK		 3
#define TERM		 4

typedef struct p4Proc p4Proc;
typedef struct p4Proc *p4ProcPtr;
typedef struct diskReqInfo diskReqInfo;
typedef struct diskReqInfo *diskReqPtr;

struct p4Proc{
    int             status;
    int             semID;
    int 			pid;
    int				wakeTime;
    
    p4ProcPtr       nextSleeping;
    
};

struct diskReqInfo{
	diskReqPtr	        next;
	
    int                 pid;
    int                 requestType;
    int                 startSector;
    int                 startTrack;
    int                 unit;
    int                 numSectors;
    char *              buffer;
    int                 semID;
    int                 status;
};

#endif
