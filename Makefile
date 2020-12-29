INCLUDES = -I./include
COMPILE_OPTS =$(INCLUDES) -I. -O2 -DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 
C = c
C_COMPILER = gcc
C_FLAGS = $(COMPILE_OPTS) $(CPPFLAGS) $(CFLAGS)
CPP =			cpp
CPLUSPLUS_COMPILER = g++
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -Wall -DBSD=1 $(CPPFLAGS) $(CXXFLAGS)
OBJ = o
LINK =$(C_COMPILER) -o
LINK_OPTS =	-L. $(LDFLAGS)
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK = ar cr
LIBRARY_LINK_OPTS =	
LIB_SUFFIX = a
LIBS_FOR_CONSOLE_APPLICATION =
LIBS_FOR_GUI_APPLICATION =
EXE =

SMS = sms.$(LIB_SUFFIX)

PREFIX = /usr/local
ALL = $(SMS)
all: $(ALL)

.$(C).$(OBJ):
	$(C_COMPILER) -c $(C_FLAGS) $<
.$(CPP).$(OBJ):
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $<

SMS_LIB_OBJS = smsRtsp.$(OBJ) smsRtspHandler.$(OBJ) smsRtspStream.$(OBJ) smsRtspSession.$(OBJ) smsRtspH264.$(OBJ) smsRtspAAC.$(OBJ) smsRtspPCM.$(OBJ) smsRtspVideoRaw.$(OBJ)

SMS_TEST_OBJS = test.$(OBJ)

LIBS =	-lpthread

sms.$(LIB_SUFFIX):	$(SMS_LIB_OBJS)
	$(LIBRARY_LINK) $@ $(SMS_LIB_OBJS)

test: $(SMS_TEST_OBJS) sms.$(LIB_SUFFIX) 
	$(C_COMPILER) $^ -o $@ $(C_FLAGS) -lpthread

clean:
	-rm -rf *.$(OBJ) $(ALL) test
