
####### Installation directory
DAO_DIR = /usr/local/dao
DAO_LIB_DIR = $(DAO_DIR)/lib
DAO_INC_DIR = $(DAO_DIR)/include
DAO_TOOL_DIR = $(DAO_DIR)/tools

DAO_MACRO = -DDAO_WITH_MACRO
DAO_THREAD = -DDAO_WITH_THREAD
DAO_NUMARRAY = -DDAO_WITH_NUMARRAY
DAO_ASYNCLASS = -DDAO_WITH_ASYNCLASS
DAO_DYNCLASS = -DDAO_WITH_DYNCLASS
DAO_DECORATOR = -DDAO_WITH_DECORATOR
DAO_SERIALIZ = -DDAO_WITH_SERIALIZATION

# TODO: enable only for compiling non-static target.
#USE_READLINE = -DDAO_USE_READLINE
LIB_READLINE = -lreadline -lncurses

DAO_CONFIG = $(DAO_MACRO) $(DAO_THREAD) $(DAO_NUMARRAY) $(DAO_SERIALIZ) $(DAO_ASYNCLASS) $(DAO_DYNCLASS) $(DAO_DECORATOR) $(USE_READLINE)

CC        = $(CROSS_COMPILE)gcc
CFLAGS    = -Wall -Wno-unused -fPIC $(DAO_CONFIG)
INCPATH   = -I. -Ikernel
LFLAGS    = -fPIC
LFLAGSDLL = -fPIC
LIBS      = -L. -ldl -lpthread -lm $(LIB_READLINE)

# dynamic linked Dao interpreter, requires dao.so to run:
TARGET   = dao

# Dao dynamic linking library
TARGETDLL	= dao.so

ARCHIVE = dao.a


CHANGESET_ID = $(shell hg id -i)

ifneq ($(CHANGESET_ID),)
  CFLAGS += -DCHANGESET_ID=\"HG.$(CHANGESET_ID)\"
endif


UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
  CFLAGS += -DUNIX
  LFLAGSDLL += -shared -Wl,-soname,libdao.so
endif

ifeq ($(UNAME), Darwin)
  TARGETDLL	= dao.dylib
  CFLAGS += -DUNIX -DMAC_OSX
  LFLAGSDLL += -dynamiclib -install_name libdao.dylib
  LIBS += -L/usr/local/lib
endif

ifeq ($(CC), gcc)
  ifeq ($(debug),yes)
    CFLAGS += -ggdb -DDEBUG
    LFLAGS += -ggdb
  else
    CFLAGS += -O2
    LFLAGS += -s
  endif
endif

ifeq ($(std),C90)
  CFLAGS += -ansi -pedantic
endif


AR = ar rcs

COPY      = cp
COPY_FILE = $(COPY) -f
COPY_DIR  = $(COPY) -r
DEL_FILE  = rm -fR
SYMLINK   = ln -sf
MKDIR     = mkdir -p
HAS_DIR = test -d
HAS_FILE = test -f

####### Output directory

OBJECTS = kernel/daoArray.o \
		  kernel/daoMap.o \
		  kernel/daoType.o \
		  kernel/daoValue.o \
		  kernel/daoContext.o \
		  kernel/daoProcess.o \
		  kernel/daoRoutine.o \
		  kernel/daoGC.o \
		  kernel/daoStdtype.o \
		  kernel/daoNamespace.o \
		  kernel/daoString.o \
		  kernel/daoStdlib.o \
		  kernel/daoMacro.o \
		  kernel/daoLexer.o \
		  kernel/daoParser.o \
		  kernel/daoThread.o \
		  kernel/daoNumtype.o \
		  kernel/daoClass.o \
		  kernel/daoConst.o \
		  kernel/daoObject.o \
		  kernel/daoSched.o \
		  kernel/daoStream.o \
		  kernel/daoVmcode.o \
		  kernel/daoVmspace.o \
		  kernel/daoRegex.o

first: all

####### Implicit rules

.SUFFIXES: .c

.c.o:
	$(CC) -c $(CFLAGS) $(INCPATH) -o $@ $<

####### Build rules

all: Makefile $(TARGETDLL) $(TARGET) $(ARCHIVE)

static:  $(OBJECTS) kernel/daoMain.o
	$(CC) $(LFLAGS) -o dao $(OBJECTS) kernel/daoMain.o $(LIBS)

one:  $(OBJECTS) kernel/daoMainv.o
	$(CC) $(LFLAGS) -o daov $(OBJECTS) kernel/daoMainv.o $(LIBS)

$(TARGET):  kernel/daoMaindl.o
	$(CC) $(LFLAGS) -o $(TARGET) kernel/daoMaindl.o $(LIBS) -ldao

$(TARGETDLL):  $(OBJECTS)
	$(CC) $(LFLAGSDLL) -o $(TARGETDLL) $(OBJECTS) $(LIBS)

$(ARCHIVE):  $(OBJECTS)
	$(AR) $(ARCHIVE) $(OBJECTS)

clean:
	-$(DEL_FILE) kernel/*.o dao.a
	-$(DEL_FILE) *~ core *.core

FORCE:

####### Install

install:
	@$(HAS_DIR) $(DAO_DIR) || $(MKDIR) $(DAO_DIR)
	@$(HAS_DIR) $(DAO_LIB_DIR) || $(MKDIR) $(DAO_LIB_DIR)
	@$(HAS_DIR) $(DAO_INC_DIR) || $(MKDIR) $(DAO_INC_DIR)
	@$(HAS_DIR) $(DAO_TOOL_DIR) || $(MKDIR) $(DAO_TOOL_DIR)

	$(HAS_FILE) addpath.dao && $(COPY_FILE) addpath.dao $(DAO_DIR)
	$(HAS_FILE) tools/autobind.dao && $(COPY_FILE) tools/autobind.dao $(DAO_TOOL_DIR)
	$(HAS_FILE) tools/autobind.dao && $(SYMLINK) $(DAO_TOOL_DIR)/autobind.dao /usr/bin/autobind.dao

	$(COPY_FILE) kernel/*.h $(DAO_INC_DIR)
	$(COPY_FILE) $(TARGET) $(TARGETDLL) kernel/dao.h dao.conf $(DAO_DIR)
	$(SYMLINK) $(DAO_DIR)/$(TARGET) /usr/bin/$(TARGET)
	$(SYMLINK) $(DAO_DIR)/$(TARGETDLL) /usr/lib/lib$(TARGETDLL)
	$(SYMLINK) $(DAO_DIR)/dao.h /usr/include/dao.h

uninstall:
	@$(HAS_FILE) /usr/bin/autobind.dao && $(DEL_FILE) /usr/bin/autobind.dao
	$(DEL_FILE) /usr/bin/$(TARGET) /usr/lib/lib$(TARGETDLL) /usr/include/dao.h $(DAO_DIR)
