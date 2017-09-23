/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include "phase2.h"
#include "usloss.h"

#include "message.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void nullifyMailBox(int);
void check_kernel_mode(char *);
void disableInterrupts(void);
void enableInterrupts(void);
void initializeInterrupts(void);

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

int nextMID = 0;
int nextSlot = 0;

// the mailboxes
mailbox MailBoxTable[MAXMBOX];

// the mailbox slots
mailSlot SlotTable[MAXSLOTS];

// array of function ptrs to system call
// handlers, ...




/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg) {
    int kid_pid;
    int status;
    
    
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");

    check_kernel_mode("start1");
    // Disable interrupts
    disableInterrupts();            // FIXME: Write method

    // Initialize the mail box table, slots, & other data structures.
    for (int i = 0; i < MAXMBOX; i++) {
        // FIXME: --- First 7 mailboxes will be assigned to interrupt handlers
        if (i < 7) {
            continue;
        }
        MailBoxTable[i].mid = i;
        nullifyMailBox(i);
        // FIXME: Relationship to PID?
    }
    
    
    // Initialize USLOSS_IntVec and system call handlers,
    // FIXME: Write handler functions
    initializeInterrupts();
    
    // allocate mailboxes for interrupt handlers.  Etc...

    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */

/*
 * Clear all data in the called mailbox. All Mailboxes call this in start1
 */
void nullifyMailBox(int mailboxIndex) {
    MailBoxTable[mailboxIndex].numSlots = -1;
    MailBoxTable[mailboxIndex].numSlotsUsed = -1;
    MailBoxTable[mailboxIndex].status = EMPTY;
}

/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size) {
    
    // Check kernelMode
    check_kernel_mode("MBoxCreate");
    // Disable Interrupts
    disableInterrupts();
    
    // If less than 0 slots-> Error
    if (slots < 0) {
        enableInterrupts();
        return -1;
        // FIXME: Add debug flag print statements
    }
    
    // If slot_size is outside acceptable range-> Error
    if (slot_size < 0 || slot_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
        // FIXME: Add debug flag print statements
    }
    
    // Find and Initialize Mailbox
    for (int i = 0; i < MAXMBOX; i++) {
        if (MailBoxTable[i].status == EMPTY) {
            MailBoxTable[i].numSlots = slots;
            MailBoxTable[i].numSlotsUsed = 0;
            MailBoxTable[i].slotSize = slot_size;
            MailBoxTable[i].status = USED;
            enableInterrupts();
            return i;
        }
    }
    
    // Enable interrupts
    enableInterrupts();
    
    // FIXME: IF MAILBOX FAILED TO CREATE ---> Return -1 (Did he say this in class?)
    
    // Return index of new mailbox in MailBoxTable
    return -1;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    
    return -404;
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size) {
    
    return -404;
} /* MboxReceive */

// FIXME: Edit block comment
/* ------------------------------------------------------------------------
 Name - check_kernel_mode
 Purpose - Checks the current OS mode.
 Parameters - none
 Returns - Returns 0 if in kernel mode,
 !0 if in user mode.
 Side Effects - enable interrupts
 ------------------------------------------------------------------------ */
void check_kernel_mode(char * procName) {
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0){
        USLOSS_Console("ERROR: Process %s called in user mode", procName);
        USLOSS_Halt(1);
    }
} /* check_kernel_mode */

/* ------------------------------------------------------------------------
 Name - initializeinterrupts
 Purpose - Initializes the interrupts required (clock interrupts).
 - Initializes a non-null value for Illegal_Int in IntVec.
 Parameters - none
 Returns - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void initializeInterrupts() {
    //USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;
    //USLOSS_IntVec[USLOSS_ILLEGAL_INT] = illegalArgumentHandler;
} /* initializeInterrupts */


/* ------------------------------------------------------------------------
 Name - disableInterrupts
 Purpose - Disable interrupts
 - Thows an error if USLOSS is passed an invalid PSR
 Parameters - none
 Returns - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void disableInterrupts() {
    
    if (USLOSS_PsrSet(USLOSS_PsrGet() ^ USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("ERROR: disableInterrupts(): Failed to disable interrupts.\n");
    }
    return;
} /* disableInterrupts */

void enableInterrupts(){}
