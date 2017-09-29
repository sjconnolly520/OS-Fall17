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
void insertProcessInSendBlockedList(int, mboxProcPtr);
void insertProcessInReceiveBlockedList(int, mboxProcPtr);
void asscociateSlotWithMailbox(int, slotPtr);
void disableInterrupts(void);
void enableInterrupts(void);
void initializeInterrupts(void);
slotPtr getAvailableSlot(void);
int MboxRelease(int);
void clearAllSlots(slotPtr);
int MboxCondSend(int, void *, int );
int MboxCondSend(int, void *, int );
int waitDevice(int, int, int *);
int check_io(void);
int isZeroSlotMailBox(int);
void clockHandler2(int, void*);
void diskHandler(int, void*);
void terminalHandler(int, void*);
void systemCallHandler(int, void*);

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
int clockHandlerCount = 0;




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
        
        nullifyMailBox(i);
        MailBoxTable[i].mid = i;
        
        // Create first seven boxes for interrupt handlers
        if (i < 7) {
            MboxCreate(0, 0);
        }
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
    mboxProcTable[processIndex].next           = NULL;
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
        printf("Message = %s\n", walker->message);
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
    if (currMBox->numSlotsUsed >= currMBox->numSlots && currMBox->recieveBlocked == NULL) {
        
        // Add process to process table and initialize fields
        int pid = getpid();
        mboxProcTable[pid % MAXPROC].pid = pid;
        mboxProcTable[pid % MAXPROC].msgSize = msg_size;
        mboxProcTable[pid % MAXPROC].message = msg_ptr;
        
        // Insert Current process into Mailbox's sendBlockList.
        insertProcessInSendBlockedList(mbox_id, &mboxProcTable[pid % MAXPROC]);
        
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
        
        currMBox->recieveBlocked = currMBox->recieveBlocked->next;
        
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
    disableInterrupts();
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
    
    if (isZeroSlotMailBox(mbox_id) && currMBox->sendBlocked != NULL ){
    	mboxProcPtr toUnblock = currMBox->sendBlocked;
    	
    	int actualMessageSize = toUnblock->msgSize;
    	
    	if (actualMessageSize > msg_size) {
        	enableInterrupts();
        	return -1;
    	}
    
    	// Copy message to process from slot.
    	memcpy(msg_ptr, toUnblock->message, actualMessageSize); 
		
		//update sendBlocked
    	currMBox->sendBlocked = toUnblock->next;
    	
    	unblockProc(toUnblock->pid);
    	return actualMessageSize;
    }
    
    // There is no message in the mailbox, Current will need to block.
    if (currMBox->numSlotsUsed <= 0){
        
        // Add process to process table and initialize fields
        int pid = getpid();
        mboxProcTable[pid % MAXPROC].pid = pid;
    	mboxProcTable[pid % MAXPROC].message = msg_ptr;
    	mboxProcTable[pid % MAXPROC].msgSize = msg_size;
    	
    	// Add process to mailbox recieve blocked list
        insertProcessInReceiveBlockedList(mbox_id, &mboxProcTable[pid % MAXPROC]);
    	
        // Block until a message is available
    	blockMe(REC_BLOCKED);
    	
    	// Check if the process was unblocked on a releasing mailbox or was zapped after an unblockProc
    	if (mboxProcTable[pid % MAXPROC].wasReleased == 1 || isZapped()){
    		enableInterrupts();
    		return -3;
    	}
    	
        // Get size of the message from processTable
    	int receivedMessageSize = mboxProcTable[pid % MAXPROC].msgSize;
        
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

/* ------------------------------------------------------------------------
 Name - MboxCondSend
 Purpose    - Sends a message to the given mailbox, but does not block
              if the mailbox is full
 Parameters - int mailboxID: the index of the MailBox to send to
            - void * message: the address of the message to send
            - int messageSize: the size of the message to send
 Returns    - Returns true (1) if the mailbox indicated is zeroSlot
 - Returns false (0) otherwise
 Side Effects - none.
 ----------------------------------------------------------------------- */
int MboxCondSend(int mailboxID, void *message, int messageSize) {
    
    // Check if in kernel mode
    check_kernel_mode("MBoxSend()");
    // DisableInterrupts
    disableInterrupts();
    
    // if processes was zapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }
    
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
    // If (msg_size > MailBox.maxSlotSize AND it is not a zeroSlotter) OR
    //    (msg_size < 0)
    if ((messageSize > MailBoxTable[mailboxID].slotSize && !isZeroSlotMailBox(mailboxID)) || messageSize < 0) {
        enableInterrupts();
        return -1;
    }
    
    // Find a slot for the new message
    slotPtr nextSlot = getAvailableSlot();
    
    // No available slot in slotTable
    if (nextSlot == NULL) {
        return -2;
    }
    
    // Grab the loction of the MailBox at mbox_id
    mBoxPtr currMBox = &MailBoxTable[mailboxID];
    
    // If there are no available slots in Mailbox
    if (currMBox->numSlotsUsed >= currMBox->numSlots && currMBox->recieveBlocked == NULL) {
        enableInterrupts();
        return -2;
    }
    
    // If there is a process receiveBlocked on this mailbox (there is a process waiting for a message), write directly to it.
    else if (currMBox->recieveBlocked != NULL){
        
        // Grab the pid of the first receiveBlocked process
        // Copy the message from Current directly to the receiveBlocked process
        int blockedPID = currMBox->recieveBlocked->pid;
        mboxProcTable[blockedPID % MAXPROC].msgSize = messageSize;
        memcpy(currMBox->recieveBlocked->message, message, messageSize);
        
        currMBox->recieveBlocked = currMBox->recieveBlocked->next;
        
        // Unblock the receiveBlocked process
        unblockProc(blockedPID);
        return 0;
    }
    
    // Insert slot into mailbox
    asscociateSlotWithMailbox(mailboxID, nextSlot);
    currMBox->numSlotsUsed++;
    
    // Assign necessary values to slot
    memcpy(nextSlot->message, message, messageSize);
    nextSlot->actualMessageSize = messageSize;
    
    // Message successfully sent
    enableInterrupts();
    return 0;
} /* MboxCondSend */

/* ------------------------------------------------------------------------
 Name - MboxCondReceive
 Purpose    - Get a msg from a slot of the indicated mailbox.
 Parameters - mailbox id, pointer to data of msg, max # of bytes that
              can be received.
 Returns    - actual size of msg if successful, -1 if invalid args.
 Side Effects - none.
 ----------------------------------------------------------------------- */
int MboxCondReceive(int mailboxID, void *message, int maxMessageSize){
    check_kernel_mode("MboxReceive()");
    disableInterrupts();
    
    // If mbox_id > MAXMBOX OR mbox_id < 0
    if (mailboxID > MAXMBOX || mailboxID < 0) {
        enableInterrupts();
        return -1;
    }
    // If mailbox doesn't exits
    if (MailBoxTable[mailboxID].status == EMPTY) {
        enableInterrupts();
        return -1;
    }
    
    // Grab the loction of the MailBox at mbox_id
    mBoxPtr currMBox = &MailBoxTable[mailboxID];
    
    // There is no message in the mailbox, Current will need to block.
    if (currMBox->numSlotsUsed <= 0){
        return -2;
    }
    
    // Get the slot which contains the message.
    slotPtr slotToRemove = currMBox->firstSlotPtr;
    
    // Get message size
    int actualMessageSize = slotToRemove->actualMessageSize;
    // Check to make sure message is not greater than buffer
    if (actualMessageSize > maxMessageSize) {
        enableInterrupts();
        return -1;
    }
    
    // Copy message to process from slot.
    memcpy(message, slotToRemove->message, actualMessageSize);
    
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
        asscociateSlotWithMailbox(mailboxID, nextSlot);
        currMBox->numSlotsUsed++;
        
        // FIXME: Nullify entry in processTable. Do we need that?
        unblockProc(procToAdd->pid);
    }
    
    // Enable interrupts and return the received message size
    
    // If calling process isZapped
    if (isZapped()) {
        enableInterrupts();
        return -3;
    }
    
    enableInterrupts();
    return actualMessageSize;
} /* MboxCondReceive */

/* ------------------------------------------------------------------------
 Name - isZeroSlotMailBox
 Purpose    - Determines if a given mailbox has zeroSlot
 Parameters - int mailboxID: the index of the MailBox in question
 Returns    - Returns true (1) if the mailbox indicated is zeroSlot
            - Returns false (0) otherwise
 Side Effects - none.
 ----------------------------------------------------------------------- */
int isZeroSlotMailBox(int mailBoxID) {
    if (MailBoxTable[mailBoxID].numSlots == 0) {
        return 1;
    }
    return 0;
} /* isZeroSlotMailBox */

/* ------------------------------------------------------------------------
 Name - waitDevice
 Purpose    - Determines which IODevice MailBox process will hold on.
 Parameters - int type: the type of interrupt device to block on
            - int unit: the index of the interrupt to block on
            - int * status: the time at which the processes unblocked.
 Returns    - int -1 if process was zapped
                   0 otherwise
 Side Effects - none.
 ----------------------------------------------------------------------- */
int waitDevice(int type, int unit, int *status){
    
    int mailboxID = -404;         // the index of the i/o mailbox
    int clockID = 0;              // index of the clock i/o mailbox
    int diskID[] = {1, 2};        // indexes of the disk i/o mailboxes
    int termID[] = {3, 4, 5, 6};  // indexes of the terminal i/o mailboxes
    
    // Determine the index of the i/o mailbox for the given device type and unit
    switch (type) {
        case USLOSS_CLOCK_INT:
            mailboxID = clockID;
            break;
            
        case USLOSS_DISK_INT:
            if (unit < 0 || unit >  1) {
                USLOSS_Console("ERROR: waitDevice(): Invalid diskDevice Unit. Halting\n");
                USLOSS_Halt(1);
            }
            mailboxID = diskID[unit];
            break;
            
        case USLOSS_TERM_INT:
            if (unit < 0 || unit >  3) {
                USLOSS_Console("ERROR: waitDevice(): Invalid Terminal unit. Halting...\n");
                USLOSS_Halt(1);
            }
            mailboxID = termID[unit];
            break;
            
        default:
            USLOSS_Console("ERROR: waitDevice(): Invalid parameter. Halting...\n");
            USLOSS_Halt(1);
    }
    
    // Wait for status of device to be returned from handler
    int returnCode = MboxReceive(mailboxID, status, sizeof(int));
    
    // If zapped
    if (returnCode == -3) {
        return -1;
    }
    return 0;
} /* waitDevice */

/* ------------------------------------------------------------------------
Name        - clockHandler2
Purpose     - Handles the logic of releasing a blocked process on the clockDevice mailbox.
Parameters  - int dev: the type of device needed // FIXME: do we actually need this variable?
            - int mboxID: the index in the MailBoxTable of the ClockDevice
              mailbox to block on.
Returns     - nothing
Side Effects - none
----------------------------------------------------------------------- */
void clockHandler2(int dev, void *args) {
    
    check_kernel_mode("clockHandler2()");
    disableInterrupts();
    
    // if (!args){
//     	USLOSS_Console("ERROR: clockHandler2(): args was null. Halting...\n");
//         USLOSS_Halt(1);
//     }
    long unit = (long)args;
    
    // Check if device is actually the clock handler AND unit is valid
    if (dev != USLOSS_CLOCK_INT || unit != 0) {
        USLOSS_Console("ERROR: clockHandler2(): Invalid parameters. Halting...\n");
        USLOSS_Halt(1);
    }
    
    int mboxID = unit;
    int status;                     // Initialize field to contain time at moment of read
    clockHandlerCount++;            // Increment how often this function has been called.
    
    // If this is the fifth time this function has been called
    if (clockHandlerCount >= 5) {
        clockHandlerCount = 0;              // Reset count
        // Fetch status from DeviceInput
        if (USLOSS_DeviceInput(USLOSS_CLOCK_INT, 0, &status) == USLOSS_DEV_INVALID) {
            USLOSS_Console("ERROR: clockHandler2(): Encountered error fetching current time. Halting.\n");
            USLOSS_Halt(1);
        }
        // Make a call to unblock the process that is receiveBlocked on the device mailbox
        MboxCondSend(mboxID, &status, sizeof(int));
    }
    
    // Call dispatcher and stuff, I guess.
    timeSlice();
    enableInterrupts();
} /* clockHandler2 */

/* ------------------------------------------------------------------------
 Name       - diskHandler
 Purpose    - called when interrupt vector is activated for this device
 Parameters - int dev: device type
            - int unit: index in device type array
 Returns    - void
 Side Effects - none
 ----------------------------------------------------------------------- */
void diskHandler(int dev, void *args) {
    check_kernel_mode("diskHandler");
    disableInterrupts();
    
    long unit = (long)args;

    // Check if device is actually the disk handler AND unit is valid
    if (dev != USLOSS_DISK_INT || unit < 0 || unit > 1) {
        USLOSS_Console("ERROR: diskHandler(): wrong device or unit. Halting...\n");
        USLOSS_Halt(1);
    }
    
    int status;                     // Initialize field to contain time at moment of read
    int mBoxID = unit + 1;          // Determine the mailbox index.
    
    // Fetch status from DeviceInput
    if (USLOSS_DeviceInput(USLOSS_DISK_INT, unit, &status) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: clockHandler2(): Encountered error fetching current time. Halting.\n");
        USLOSS_Halt(1);
    }
    MboxCondSend(mBoxID, &status, sizeof(int));
    enableInterrupts();
} /* diskHandler */

/* ------------------------------------------------------------------------
 Name       - terminalHandler
 Purpose    - called when interrupt vector is activated for this device
 Parameters - int dev: device type
            - int unit: index in device type array
 Returns    - void
 Side Effects - none
 ----------------------------------------------------------------------- */
void terminalHandler(int dev, void *args) {
    check_kernel_mode("termHandler");
    disableInterrupts();
    
    long unit = (long)args;
    
    // Check if device is actually the disk handler AND unit is valid
    if (dev != USLOSS_TERM_INT || unit < 0 || unit > 3) {
        USLOSS_Console("termHandler(): wrong device or unit\n");
        USLOSS_Halt(1);
    }
    
    int status;                     // Initialize field to contain time at moment of read
    int mBoxID = unit + 3;          // Determine the mailbox index.
    
    // Fetch status from DeviceInput
    if (USLOSS_DeviceInput(USLOSS_TERM_INT, unit, &status) == USLOSS_DEV_INVALID) {
        USLOSS_Console("ERROR: clockHandler2(): Encountered error fetching current time. Halting.\n");
        USLOSS_Halt(1);
    }
    
    MboxCondSend(mBoxID, &status, sizeof(int));
    enableInterrupts();
} /* terminalHandler */

/* ------------------------------------------------------------------------
 Name - syscallHandler
 Purpose - called when interrupt vector is activated for this device
 Parameters - device, unit
 Returns - void
 Side Effects - none
 ----------------------------------------------------------------------- */
void systemCallHandler(int dev, void *unit) {
    
}

/* ------------------------------------------------------------------------
 Name - insertProcessInSendBLockedList
 Purpose     - Inserts a process into the send block list of a given mailbox
 Parameters  - int mBoxID: the index of the mailbox in the MailBoxTable
             - mboxProcPtr procToAdd: a pointer to the location in the
               ProcessTable which contains the process to add to the
               mailBox's sendBlockedList
 Returns     - nothing
 Side Effects - none.
 ----------------------------------------------------------------------- */
void insertProcessInSendBlockedList(int mBoxID, mboxProcPtr procToAdd) {
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
 Name - insertProcessInReceiveBLockedList
 Purpose     - Inserts a process into the send block list of a given mailbox
 Parameters  - int mBoxID: the index of the mailbox in the MailBoxTable
             - mboxProcPtr procToAdd: a pointer to the location in the
               ProcessTable which contains the process to add to the
               mailBox's sendBlockedList
 Returns     - nothing
 Side Effects - none.
 ----------------------------------------------------------------------- */
void insertProcessInReceiveBlockedList(int mBoxID, mboxProcPtr procToAdd) {
    if (MailBoxTable[mBoxID].recieveBlocked == NULL) {
        MailBoxTable[mBoxID].recieveBlocked = procToAdd;
        return;
    }
    mboxProcPtr walker = MailBoxTable[mBoxID].recieveBlocked;
    while (walker->next != NULL) {
        walker = walker->next;
    }
    walker->next = procToAdd;
    procToAdd->next = NULL;
} /* insertProcessInReceiveBLockedList */

/* ------------------------------------------------------------------------
 Name - asscociateSlotWithMailbox
 Purpose     - Inserts a slot into the SlotList of a given mailbox
 Parameters  - int mBoxID: the index of the mailbox in the MailBoxTable
             - slotPtr slotToInsert: a pointer to the location in the
               SlotTable which contains the slot to add to the
               mailBox's slotList
 Returns     - nothing
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

// FIXME: Edit block comment
/* ------------------------------------------------------------------------
 Name - check_kernel_mode
 Purpose    - Checks the current OS mode.
            - Thows an error if USLOSS is passed an invalid PSR
 Parameters - char * procName: the name of the process calling the function
              (or more likely, the function).
 Returns    - nothing
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
 Purpose     - Initializes the interrupts required (clock interrupts).
             - Initializes a non-null value for Illegal_Int in IntVec.
 Parameters  - none
 Returns     - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void initializeInterrupts() {
//    USLOSS_IntVec[USLOSS_ILLEGAL_INT] = (void*)illegalArgumentHandler;
    USLOSS_IntVec[USLOSS_CLOCK_INT] = (void*)clockHandler2;
    USLOSS_IntVec[USLOSS_DISK_INT] = (void*)diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = (void*)terminalHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = (void*)systemCallHandler;
} /* initializeInterrupts */

/* ------------------------------------------------------------------------
 Name - disableInterrupts
 Purpose    - Disable interrupts
            - Thows an error if USLOSS is passed an invalid PSR
 Parameters - none
 Returns    - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void disableInterrupts() {
    
    if (USLOSS_PsrSet(USLOSS_PsrGet() ^ USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR){
        USLOSS_Console("ERROR: disableInterrupts(): Failed to disable interrupts.\n");
        USLOSS_Halt(1);
    }
    return;
} /* disableInterrupts */

/* ------------------------------------------------------------------------
 Name - enableInterrupts
 Purpose    - Enables interrupts
            - Thows an error if USLOSS is passed an invalid PSR
 Parameters - none
 Returns    - nothing
 Side Effects - none
 ----------------------------------------------------------------------- */
void enableInterrupts(){
    if (USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT) == USLOSS_ERR_INVALID_PSR) {
        USLOSS_Console("ERROR: enableInterrupts(): Failed to enable interrupts.\n");
        USLOSS_Halt(1);
    }
} /* enableInterrupts */

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
    
    mboxProcPtr tempSend = currMBox->sendBlocked;
    mboxProcPtr tempRec  = currMBox->recieveBlocked;
    
    // If mailbox has slots, nullify them
    if (currMBox->firstSlotPtr != NULL){
        clearAllSlots(currMBox->firstSlotPtr);
    }
        
    // Nullify remaining fields in mailbox.
    nullifyMailBox(mailboxID);
    
    // Reactivate and terminate all processes on mailbox sendBlockedList
	while(tempSend != NULL){
        // Set the "released on mailbox" flag to true
		tempSend->wasReleased = 1;
        
        // Grab the pid of the prcess which is being unblocked.
        // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
		int tempPid = tempSend->pid;
		tempSend = tempSend->next;
		unblockProc(tempPid);
        
        // Disable interrupts when returning to this function.
		disableInterrupts();
	}
	
    // Reactivate and terminate all processes on mailbox receiveBlockedList
	while(tempRec != NULL){
        // Set the "released on mailbox" flag to true
		tempRec->wasReleased = 1;
        
        // Grab the pid of the prcess which is being unblocked.
        // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
		int tempPid = tempRec->pid;
		tempRec = tempRec->next;
		unblockProc(tempPid);
        
        // Disable interrupts when returning to this function.
		disableInterrupts();
	}
	// while(currMBox->sendBlocked != NULL){
//         // Set the "released on mailbox" flag to true
// 		currMBox->sendBlocked->wasReleased = 1;
//         
//         // Grab the pid of the prcess which is being unblocked.
//         // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
// 		int tempPid = currMBox->sendBlocked->pid;
// 		currMBox->sendBlocked = currMBox->sendBlocked->next;
// 		unblockProc(tempPid);
//         
//         // Disable interrupts when returning to this function.
// 		disableInterrupts();
// 	}
// 	
//     // Reactivate and terminate all processes on mailbox receiveBlockedList
// 	while(currMBox->recieveBlocked != NULL){
//         // Set the "released on mailbox" flag to true
// 		currMBox->recieveBlocked->wasReleased = 1;
//         
//         // Grab the pid of the prcess which is being unblocked.
//         // Remove the process from the sendBlockList of Mailbox and unblock it so it may finish.
// 		int tempPid = currMBox->recieveBlocked->pid;
// 		currMBox->recieveBlocked = currMBox->recieveBlocked->next;
// 		unblockProc(tempPid);
//         
//         // Disable interrupts when returning to this function.
// 		disableInterrupts();
// 	}
	
	// If mailbox has slots, nullify them
    // if (currMBox->firstSlotPtr != NULL){
//         clearAllSlots(currMBox->firstSlotPtr);
//     }
        
    // Nullify remaining fields in mailbox.
//     nullifyMailBox(mailboxID);
        
    enableInterrupts(); 
      
    //TODO check a isZapped call from entry to MboxRelease
    if (isZapped()) {
        return -3;
    }
    
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

/* ------------------------------------------------------------------------
 Name - check_io
 Purpose    - checks if any processes are receiveBlocked on any io_device_mailboxes
 Parameters - none
 Returns    - Boolean: 1 if there are any receiveBlocked Processes, 0 otherwise
 Side Effects - none
 ----------------------------------------------------------------------- */
int check_io() {
    for (int i = 0; i < 7; i++) {
        if (MailBoxTable[i].recieveBlocked != NULL) {
            return 1;
        }
    }
    return 0;
} /* check_io */
