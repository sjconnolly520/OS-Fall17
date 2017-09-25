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
void nullifyProc(int);
void check_kernel_mode(char *);
void insertProcessInSendBLockedList(int, mboxProcPtr);
void asscociateSlotWithMailbox(int, slotPtr);
void disableInterrupts(void);
void enableInterrupts(void);
void initializeInterrupts(void);
slotPtr getAvailableSlot(void);
int MboxRelease(int);
void clearAllSlots(slotPtr);

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
   Purpose    - Initializes mailboxes and interrupt vector.
              - Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns    - one to indicate normal quit.
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

    // Initialize the mail box table
    for (int i = 0; i < MAXMBOX; i++) {
        
        // FIXME: --- First 7 mailboxes will be assigned to interrupt handlers
        if (i < 7) {
            continue;
        }
        
        nullifyMailBox(i);
        MailBoxTable[i].mid = i;
    }
    
    // Initialize slotTable
    for (int i = 0; i < MAXSLOTS; i++) {
        nullifySlot(i);
        SlotTable[i].slotID = i;
    }
    
    // Initialize mboxProcTable
    for (int i = 0; i < MAXPROC; i++){
        nullifyProc(i);
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

/* ------------------------------------------------------------------------
 Name - nullifyMailBox
 Purpose     - Sets all fields in a mailbox to their starting values.
             - All mailboxes call this function in start1()
 Parameters  - int mailboxIndex: The index in the MailBoxTable which
               contains the mailbox to reset values.
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void nullifyMailBox(int mailboxIndex) {
    MailBoxTable[mailboxIndex].numSlots             = -1;
    MailBoxTable[mailboxIndex].numSlotsUsed         = -1;
    MailBoxTable[mailboxIndex].status               = EMPTY;
    MailBoxTable[mailboxIndex].recieveBlocked       = NULL;
    MailBoxTable[mailboxIndex].sendBlocked          = NULL;
    MailBoxTable[mailboxIndex].slotSize             = -1;
} /* nullifyMailBox */

/* ------------------------------------------------------------------------
 Name - nullifySlot
 Purpose    - Sets all fields in a slot to their starting values.
            - All slots call this function in start1()
 Parameters - int slotIndex: The index in the SlotTable which
              contains the slot to reset values.
 Returns    - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void nullifySlot(int slotIndex) {
    SlotTable[slotIndex].status                     = EMPTY;
    SlotTable[slotIndex].siblingSlotPtr             = NULL;
    SlotTable[slotIndex].message[0]                 = '\0';
} /* nullifySlot */

/* ------------------------------------------------------------------------
 Name - nullifyProc
 Purpose    - Sets all fields in a process to their starting values.
            - All processes call this function in start1()
 Parameters - int process: The index in the mboxProcTable which
              contains the process to reset values.
 Returns    - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void nullifyProc(int processIndex) {
    mboxProcTable[processIndex].pid            = -1;
    mboxProcTable[processIndex].msgSize        = -1;
    mboxProcTable[processIndex].message        = NULL;
    mboxProcTable[processIndex].wasReleased    = 0;
} /* nullifyProc */

/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose    - gets a free mailbox from the table of mailboxes and initializes it
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns    - -1 to indicate that no mailbox was created, or a value >= 0 as the
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
    }
    
    // If slot_size is outside acceptable range-> Error
    if (slot_size < 0 || slot_size > MAX_MESSAGE) {
        enableInterrupts();
        return -1;
    }
    
    // Find and Initialize a Mailbox
    for (int i = 0; i < MAXMBOX; i++) {
        if (MailBoxTable[i].status == EMPTY) {
            MailBoxTable[i].numSlots = slots;
            MailBoxTable[i].numSlotsUsed = 0;
            MailBoxTable[i].slotSize = slot_size;
            MailBoxTable[i].status = USED;
            enableInterrupts();
            // Return index of new mailbox in MailBoxTable
            // This return statement fires if everything went perfectly.
            return i;
        }
    }
    
    // Enable interrupts
    enableInterrupts();
    
    // Failed to create a mailbox
    return -1;
} /* MboxCreate */


/*
 * This method is probably trash. It prints all messages stored in a mailbox's slots.
 */
void printMBoxSlotContents(int mbox_id) {
    
    slotPtr walker = MailBoxTable[mbox_id].firstSlotPtr;
    
    while (walker != NULL) {
        printf("%s\n", walker->message);
        walker = walker->siblingSlotPtr;
    }
} /* printMBoxSlotContents */

/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose    - Put a message into a slot for the indicated mailbox.
              - Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns    - zero if successful, -1 if invalid args.
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
    
    // Grab the loction of the MailBox at mbox_id
    mBoxPtr currMBox = &MailBoxTable[mbox_id];
    
    // If there are no available slots in Mailbox, Current sendBlocks
    if (currMBox->numSlotsUsed >= currMBox->numSlots) {
        
        // Add process to process table and initialize fields
        int pid = getpid();
        mboxProcTable[pid % MAXPROC].pid = pid;
        mboxProcTable[pid % MAXPROC].msgSize = msg_size;
        mboxProcTable[pid % MAXPROC].message = msg_ptr;
        
        // Insert Current process into Mailbox's sendBlockList.
        insertProcessInSendBLockedList(mbox_id, &mboxProcTable[pid % MAXPROC]);
        
        // Block until there is space to send to
        blockMe(SEND_BLOCKED);
        
        // Check if the process was unblocked on a releasing mailbox or was zapped after an unblockProc
    	if (mboxProcTable[pid % MAXPROC].wasReleased == 1 || isZapped()){
    		enableInterrupts();
    		return -3;
    	}
    	
        return 0;
    }
    
    // If there is a process receiveBlocked on this mailbox (there is a process waiting for a message), write directly to it.
    else if (currMBox->recieveBlocked != NULL){
        
        // Grab the pid of the first receiveBlocked process
        // Copy the message from Current directly to the receiveBlocked process
    	int blockedPID = currMBox->recieveBlocked->pid;
    	mboxProcTable[blockedPID % MAXPROC].msgSize = msg_size;
    	memcpy(currMBox->recieveBlocked->message, msg_ptr, msg_size);
    	
        // Unblock the receiveBlocked process
    	unblockProc(blockedPID);
    	return 0;
    }
    
    // Insert slot into mailbox
    asscociateSlotWithMailbox(mbox_id, nextSlot);
    currMBox->numSlotsUsed++;
    
    // Assign necessary values to slot
    memcpy(nextSlot->message, msg_ptr, msg_size);
    nextSlot->actualMessageSize = msg_size;
        
    // Message successfully sent
    enableInterrupts();
    return 0;
} /* MboxSend */

/* ------------------------------------------------------------------------
 Name - insertProcessInSendBLockedList
 Purpose    - Inserts a process into the send block list of a given mailbox
 Parameters - int mBoxID: the index of the mailbox in the MailBoxTable
            - mboxProcPtr procToAdd: a pointer to the location in the
              ProcessTable which contains the process to add to the
              mailBox's sendBlockedList
 Returns    - nothing
 Side Effects - none.
 ----------------------------------------------------------------------- */
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
} /* insertProcessInSendBLockedList */

/* ------------------------------------------------------------------------
 Name - asscociateSlotWithMailbox
 Purpose    - Inserts a slot into the SlotList of a given mailbox
 Parameters - int mBoxID: the index of the mailbox in the MailBoxTable
            - slotPtr slotToInsert: a pointer to the location in the
              SlotTable which contains the slot to add to the
              mailBox's slotList
 Returns    - nothing
 Side Effects - none.
 ----------------------------------------------------------------------- */
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
} /* asscociateSlotWithMailbox */

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
    
    // Grab the loction of the MailBox at mbox_id
    mBoxPtr currMBox = &MailBoxTable[mbox_id];
    
    // There is no message in the mailbox, Current will need to block.
    if (currMBox->numSlotsUsed <= 0){
        
        // Add process to process table and initialize fields
        int pid = getpid();
        mboxProcTable[pid % MAXPROC].pid = pid;
    	mboxProcTable[pid % MAXPROC].message = msg_ptr;
    	mboxProcTable[pid % MAXPROC].msgSize = msg_size;
    	
    	// Add process to mailbox recieve blocked list
        // FIXME: Add to end of linked list of ReceiveBlockedProcesses
    	currMBox->recieveBlocked = &mboxProcTable[getpid() % MAXPROC];
    	
        // Block until a message is available
    	blockMe(REC_BLOCKED);
    	
    	// Check if the process was unblocked on a releasing mailbox or was zapped after an unblockProc
    	if (mboxProcTable[pid % MAXPROC].wasReleased == 1 || isZapped()){
    		enableInterrupts();
    		return -3;
    	}
    	
        // Get size of the message from processTable
    	int receivedMessageSize = mboxProcTable[pid % MAXPROC].msgSize;
        
        // FIXME: I'm not certain this is correct. What if this process is waiting on multiple receives?
        nullifyProc(pid % MAXPROC);
        
    	// Re-enable and return message size
    	enableInterrupts();
    	return receivedMessageSize;
    }
    
    // Get the slot which contains the message.
    slotPtr slotToRemove = currMBox->firstSlotPtr;
    
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
    currMBox->firstSlotPtr = slotToRemove->siblingSlotPtr;
    nullifySlot(slotToRemove->slotID);
    currMBox->numSlotsUsed--;
    
    // If there are messages waiting for a slot (there is a sendBlock on this mailbox), give the first a slot.
    if (currMBox->sendBlocked != NULL) {
        
        // Get first sendBlocked Process and remove it from sendBlockedList
        mboxProcPtr procToAdd = currMBox->sendBlocked;
        currMBox->sendBlocked = currMBox->sendBlocked->next;
        
        // Get a slot for new process, copy message from process to slot.
        slotPtr nextSlot = getAvailableSlot();
        memcpy(nextSlot->message, procToAdd->message, procToAdd->msgSize);
        nextSlot->actualMessageSize = procToAdd->msgSize;
        
        // FIXME: Do we need to nullify procToAdd->next?
        
        // Add the slot to the mailbox
        asscociateSlotWithMailbox(mbox_id, nextSlot);
        currMBox->numSlotsUsed++;
        
        // FIXME: Nullify entry in processTable. Do we need that?
        unblockProc(procToAdd->pid);
    }
    
    // Enable interrupts and return the received message size
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

/* ------------------------------------------------------------------------
 Name - getAvailableSlot
 Purpose    - Finds the first available slot in the slot table
 Parameters - none
 Returns    - A pointer to the first available slot in the SlotTable.
            - NULL if there is no available slot.
 Side Effects - none
 ----------------------------------------------------------------------- */
slotPtr getAvailableSlot() {
    // Find and return a pointer to the first available slot
    for (int i = 0; i < MAXSLOTS; i++) {
        if (SlotTable[i].status == EMPTY) {
            SlotTable[i].status = USED;
            return &SlotTable[i];
        }
    }
    return NULL;
} /* getAvailableSlot */

/* ------------------------------------------------------------------------
 Name - MboxRelease
 Purpose    - Responsibly destroys a given mailbox
 Parameters - int mailboxID: the index of the mailbox to destroy
 Returns    - -3 If the process was zapped while releasing th mailbox
            - -1 If the mailbox in the MailBoxTable at mailboxID
                 is an illegal target.
            - 0  If the mailbox is release properly
 Side Effects - none
 ----------------------------------------------------------------------- */
int MboxRelease(int mailboxID) {
	disableInterrupts();
	check_kernel_mode("MboxRelease");
	
	// Error checking for parameters
    // If mbox_id is out of bounds
    if (mailboxID > MAXMBOX || mailboxID < 0) {
        enableInterrupts();
        return -1;
    }
    // If mailbox doesn't exits
    if (MailBoxTable[mailboxID].status == EMPTY) {
        enableInterrupts();
        return -1;
    }
    
    // Grab the loction of the MailBox at mailboxID
    mBoxPtr currMBox = &MailBoxTable[mailboxID];
    
    // If there are no processes blocked on the given mailbox.
    if (currMBox->sendBlocked == NULL &&
        currMBox->recieveBlocked == NULL ){
        
        // If the mailbox has slots, clear them
        if (currMBox->firstSlotPtr != NULL){
        	clearAllSlots(currMBox->firstSlotPtr);
        }
        
        // Clear the rest of the fields in the mailbox
        nullifyMailBox(mailboxID);
        
        enableInterrupts();
        //TODO check a isZapped call from entry to MboxRelease
        if (isZapped()) {
            return -3;
        }
        return 0;
    }
    
    // Reactivate and terminate all processes on mailbox sendBlockedList
	while(currMBox->sendBlocked != NULL){
        // Set the "released on mailbox" flag to true
		currMBox->sendBlocked->wasReleased = 1;
        
        // Grab the pid of the prcess which is being unblocked.
        // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
		int tempPid = currMBox->sendBlocked->pid;
		currMBox->sendBlocked = currMBox->sendBlocked->next;
		unblockProc(tempPid);
        
        // Disable interrupts when returning to this function.
		disableInterrupts();
	}
	
    // Reactivate and terminate all processes on mailbox receiveBlockedList
	while(currMBox->recieveBlocked != NULL){
        // Set the "released on mailbox" flag to true
		currMBox->recieveBlocked->wasReleased = 1;
        
        // Grab the pid of the prcess which is being unblocked.
        // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
		int tempPid = currMBox->recieveBlocked->pid;
		currMBox->recieveBlocked = currMBox->recieveBlocked->next;
		unblockProc(tempPid);
        
        // Disable interrupts when returning to this function.
		disableInterrupts();
	}
	
	// If mailbox has slots, nullify them
    if (currMBox->firstSlotPtr != NULL){
        clearAllSlots(currMBox->firstSlotPtr);
    }
        
    // Nullify remaining fields in mailbox.
    nullifyMailBox(mailboxID);
        
    enableInterrupts(); 
      
    //TODO check a isZapped call from entry to MboxRelease
    if (isZapped()) return -3;
    
    return 0;
} /* MboxRelease */

/* ------------------------------------------------------------------------
 Name - clearAllSlots
 Purpose    - Clears all slots in the linked list starting at the give slot
 Parameters - slotPtr slotToClear: the index of the mailbox to destroy
 Returns    - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void clearAllSlots(slotPtr slotToClear){
	slotPtr temp = slotToClear;
	while(slotToClear->siblingSlotPtr != NULL){
		slotToClear = slotToClear->siblingSlotPtr;
		nullifySlot(temp->slotID);
		temp = slotToClear;
	}
	nullifySlot(temp->slotID);
} /* clearAllSlots */
