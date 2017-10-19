//
//  sems.h
//
//
//  Created by Stephen Connolly on 10/7/17.
//

#ifndef sems_h
#define sems_h

// min and max priorities
#define MINPRIORITY     5
#define MAXPRIORITY     1

typedef struct p3Proc p3Proc;
typedef struct p3Proc *p3ProcPtr;

struct p3Proc{
    int             status;
    int             mboxID;
    int 			pid;
    
    p3ProcPtr       children;
    p3ProcPtr       nextSibling;
    p3ProcPtr		nextSemBlocked;
    
    char            name[MAXNAME];
    int             (*startFunc)(char *);
    char            args[MAXARG];
    
};

typedef struct semStruct{
	int             status;
    int             mboxID;
    int				flags;
    
    p3ProcPtr       blockList;
} semStruct;
#endif /* sems_h */
