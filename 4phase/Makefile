PREFIX = ${HOME}

TARGET = libphase4.a
ASSIGNMENT = 452phase4
CC = gcc
AR = ar

COBJS = phase4.o libuser.o
CSRCS = ${COBJS:.o=.c}

PHASE1LIB = patrickphase1
PHASE2LIB = patrickphase2
PHASE3LIB = patrickphase3
#PHASE1LIB = patrickphase1debug
#PHASE2LIB = patrickphase2debug

HDRS = libuser.h driver.h phase1.h phase2.h phase3.h phase4.h

INCLUDE = ${PREFIX}/include

CFLAGS = -Wall -g -std=gnu99 -I. -I${INCLUDE}

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
    CFLAGS += -D_XOPEN_SOURCE
endif

LDFLAGS += -L. -L${PREFIX}/lib

TESTDIR = testcases

TESTS = test00 test01 test02 test03 test04 test05 test06 test07 test08 \
test09 test10 test11 test12 test13 test14 test15 test16 test17 \
test18

LIBS = -lusloss -l$(PHASE1LIB) -l$(PHASE2LIB) -l$(PHASE3LIB) -lphase4

all: test00 test01
test02 test03 test04 test05 test06 test07 test08 \
test09 test10 test11 test12 test13 test14 test15 test16 test17 \
test18

$(TARGET):    $(COBJS)
    $(AR) -r $@ $(COBJS)

$(TESTS):    $(TARGET)
    $(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
    $(CC) $(LDFLAGS) -o $@ $@.o $(LIBS)

clean:
    rm -f $(COBJS) $(TARGET) test*.txt test??.o test?? core term*.out

phase4.o:    driver.h

submit: $(CSRCS) $(HDRS) Makefile
    tar cvzf phase4.tgz $(CSRCS) $(HDRS) Makefile

