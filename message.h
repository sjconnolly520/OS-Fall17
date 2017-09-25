
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mailSlot  mailSlot;
typedef struct mboxProc *mboxProcPtr;
typedef struct mboxProc  mboxProc;
struct mailbox {
    int         mid;
    // other items as needed...
    int         numSlots;
    int         numSlotsUsed;
    int         slotSize;
    slotPtr     firstSlotPtr;
    int         status;
    mboxProcPtr recieveBlocked;
    mboxProcPtr sendBlocked;
    // LinkedListForMessageStoring FIXME:
    // status
};

struct mailSlot {
    int         mboxID;
    int         slotID;
    int         status;
    int         slotSize;
    int         actualMessageSize;
    slotPtr     siblingSlotPtr;
    // other items as needed...
    char      message[MAX_MESSAGE];
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

struct mboxProc {
	int 	    pid;
	void        *message;
	int 	    msgSize;
    mboxProcPtr next;
    int 	    wasReleased;
};


#define EMPTY           -1
#define USED             1

#define SEND_BLOCKED    11
#define REC_BLOCKED     12
