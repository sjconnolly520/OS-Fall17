
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mailSlot  mailSlot;
typedef struct mboxProc *mboxProcPtr;

struct mailbox {
    int         mid;
    // other items as needed...
    int         numSlots;
    int         numSlotsUsed;
    int         slotSize;
    slotPtr     firstSlotPtr;
    int         status;
    // blockSendList
    // blockRecieveList
    // LinkedListForMessageStoring FIXME:
    // status
};

struct mailSlot {
    int         mboxID;
    int         status;
    int         slotSize;
    mailSlot *  siblingSlotPtr;
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


#define EMPTY       -1
#define USED         1
