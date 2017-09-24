/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include "phase2.h"
#include "usloss.h"

#include "message.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void nullifyMailBox(int);
void nullifySlot(int);
void check_kernel_mode(char *);
void insertProcessInSendBLockedList(int, mboxProcPtr);
void asscociateSlotWithMailbox(int, slotPtr);
void disableInterrupts(void);
void enableInterrupts(void);
void initializeInterrupts(void);
slotPtr getAvailableSlot(void);

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

int nextMID = 0;
int nextSlot = 0;

// the mailboxes
mailbox MailBoxTable[MAXMBOX];

// the mailbox slots
mailSlot SlotTable[MAXSLOTS];

mboxProc mboxProcTable[50];

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
    
    // Initialize slotTable
    for (int i = 0; i < MAXSLOTS; i++) {
        nullifySlot(i);
        SlotTable[i].slotID = i;
    }
    
    // Initialize mboxProcTable
    for (int i = 0; i < 50; i++){
    	mboxProcTable[i].pid 		= -1;
    	mboxProcTable[i].msgSize 	= -1;
    	mboxProcTable[i].message 	= NULL;
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
    // FIXME: Initialize other fields.
}

/*
 * Clear all data in the called slot. All slots call this in start1
 */
void nullifySlot(int slotIndex) {
    SlotTable[slotIndex].status = EMPTY;
    SlotTable[slotIndex].siblingSlotPtr = NULL;
    SlotTable[slotIndex].message[0] = '\0';
    
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
            // Return index of new mailbox in MailBoxTable
            return i;
        }
    }
    
    // Enable interrupts
    enableInterrupts();
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
    
    // Check if in kernel mode
    check_kernel_mode("MBoxSend()");
    // DisableInterrupts
    disableInterrupts();
    
    // Error checking for parameters
    // If mbox_id is out of bounds
    if (mbox_id > MAXMBOX || mbox_id < 0) {
        enableInterrupts();
        return -1;
    }
    // If mailbox doesn't exits
    if (MailBoxTable[mbox_id].status == EMPTY) {
        enableInterrupts();
        return -1;
    }
    // If msg_size > MailBox.maxSlotSize OR msg_size < 0
    if (msg_size > MailBoxTable[mbox_id].slotSize || msg_size < 0) {
        enableInterrupts();
        return -1;
    }
    
    // Find a slot for the new message
    slotPtr nextSlot = getAvailableSlot();
    
    // No available slot in slotTable
    if (nextSlot == NULL) {
        USLOSS_Console("ERROR: MboxSend(): Slot table is full.");
        USLOSS_Halt(1);
    }
    
    // If there are no available slots in Mailbox
    if (MailBoxTable[mbox_id].numSlotsUsed >= MailBoxTable[mbox_id].numSlots) {
        // Add process to process table
        mboxProcTable[getpid() % MAXPROC].pid = getpid();
        mboxProcTable[getpid() % MAXPROC].msgSize = msg_size;
        mboxProcTable[getpid() % MAXPROC].message = msg_ptr;
        
        // Insert Current process into Mailbox's sendBlockList.
        insertProcessInSendBLockedList(mbox_id, &mboxProcTable[getpid() % MAXPROC]);
        
        blockMe(SEND_BLOCKED);
        return 0;
    }
    
    else if (MailBoxTable[mbox_id].recieveBlocked){
    	int blockedPID = MailBoxTable[mbox_id].recieveBlocked->pid;
    	mboxProcTable[blockedPID % MAXPROC].msgSize = msg_size;
    	memcpy(MailBoxTable[mbox_id].recieveBlocked->message, msg_ptr, msg_size);
    	
    	unblockProc(blockedPID);
    	return 0;
    }
    
    // Insert slot into mailbox
    asscociateSlotWithMailbox(mbox_id, nextSlot);
    MailBoxTable[mbox_id].numSlotsUsed++;
    
    // Assign necessary values to slot
    memcpy(nextSlot->message, msg_ptr, msg_size);
    nextSlot->actualMessageSize = msg_size;
        
    // Message successfully sent
    enableInterrupts();
    return 0;
} /* MboxSend */

void insertProcessInSendBLockedList(int mBoxID, mboxProcPtr procToAdd) {
    if (MailBoxTable[mBoxID].sendBlocked == NULL) {
        MailBoxTable[mBoxID].sendBlocked = procToAdd;
        return;
    }
    mboxProcPtr walker = MailBoxTable[mBoxID].sendBlocked;
    while (walker->next != NULL) {
        walker = walker->next;
    }
    walker->next = procToAdd;
    procToAdd->next = NULL;
}


/*
 *
 */
void asscociateSlotWithMailbox(int mboxID, slotPtr slotToInsert){
    if (MailBoxTable[mboxID].firstSlotPtr == NULL) {
        MailBoxTable[mboxID].firstSlotPtr = slotToInsert;
        return;
    }
    
    slotPtr walker = MailBoxTable[mboxID].firstSlotPtr;
    while (walker->siblingSlotPtr != NULL) {
        walker = walker->siblingSlotPtr;
    }
    
    walker->siblingSlotPtr = slotToInsert;
    slotToInsert->siblingSlotPtr = NULL;
}


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
    
    check_kernel_mode("MboxReceive()");
    
    //disable interrupts
    
    // If mbox_id > MAXMBOX OR mbox_id < 0
    if (mbox_id > MAXMBOX || mbox_id < 0) {
        enableInterrupts();
        return -1;
    }
    // If mailbox doesn't exits
    if (MailBoxTable[mbox_id].status == EMPTY) {
        enableInterrupts();
        return -1;
    }
    
    
    // There is no message in the mailbox, Current will need to block.
    if (MailBoxTable[mbox_id].numSlotsUsed <= 0){
    	//no mail to receive... block
    	mboxProcTable[getpid() % MAXPROC].pid = getpid();
    	mboxProcTable[getpid() % MAXPROC].message = msg_ptr;
    	mboxProcTable[getpid() % MAXPROC].msgSize = msg_size;
    	
    	//add to mailbox recieve list
        // FIXME: Add to end of linked list of ReceiveBlockedProcesses
    	MailBoxTable[mbox_id].recieveBlocked = &mboxProcTable[getpid() % MAXPROC];
    	
    	//enableInterrupts();
    	blockMe(REC_BLOCKED);
    	//disableInterrupts();
    	
    	int receivedMessageSize = mboxProcTable[getpid() % MAXPROC].msgSize;
        
        // FIXME: Write method for nullifyProcessInProcTable
    	mboxProcTable[getpid() % MAXPROC].pid = -1;
    	mboxProcTable[getpid() % MAXPROC].msgSize = -1;
    	//re-enable and return
    	enableInterrupts();
    	return receivedMessageSize;
    }
    
    // Get the slot which contains the message.
    slotPtr slotToRemove =MailBoxTable[mbox_id].firstSlotPtr;
    
    // Get message size
    int actualMessageSize = slotToRemove->actualMessageSize;
    // Check to make sure message is not greater than buffer
    if (actualMessageSize > msg_size) {
        enableInterrupts();
        return -1;
    }
    
    // Copy message to process from slot.
    memcpy(msg_ptr, slotToRemove->message, actualMessageSize);
    
    // Remove slot from mailbox (disassociate slot from mailbox), nullify slot, decrement numSlotsUsed.
    MailBoxTable[mbox_id].firstSlotPtr = slotToRemove->siblingSlotPtr;
    nullifySlot(slotToRemove->slotID);
    MailBoxTable[mbox_id].numSlotsUsed--;
    
    // If there are messages waiting for a slot (there is a sendBlock on this mailbox), give it a slot.
    if (MailBoxTable[mbox_id].sendBlocked != NULL) {
        
        // Get first sendBlocked Process and remove it from sendBlockedList
        mboxProcPtr procToAdd = MailBoxTable[mbox_id].sendBlocked;
        MailBoxTable[mbox_id].sendBlocked = MailBoxTable[mbox_id].sendBlocked->next;
        
        // Get a slot for new process, copy message from process to slot.
        slotPtr nextSlot = getAvailableSlot();
        memcpy(nextSlot->message, procToAdd->message, procToAdd->msgSize);
        nextSlot->actualMessageSize = procToAdd->msgSize;
        // FIXME: Do we need to nullify procToAdd->next?
        
        // FIXME: Nullify entry in processTable. Do we need that?
        unblockProc(procToAdd->pid);
    }
    
    enableInterrupts();
    return actualMessageSize;
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

// FIXME: DOCUMENTATION
void enableInterrupts(){}

slotPtr getAvailableSlot() {
    // Find and return a pointer to the first available slot
    for (int i = 0; i < MAXSLOTS; i++) {
        if (SlotTable[i].status == EMPTY) {
            return &SlotTable[i];
        }
    }
    return NULL;
}
