CC = gcc
AR = ar
DEBUG = 1
OSTYPE = $(shell uname -s)

CFLAGS = -Wall -I.

ifneq ($(DEBUG), 0)
CFLAGS += -g -DDEBUG
else
CFLAGS += -O2
endif

ifeq ($(OSTYPE), MorphOS)
CFLAGS += -noixemul -DAROS_ALMOST_COMPATIBLE
else
ifeq ($(OSTYPE), AROS)
# I shouldn't need to do this manually
CFLAGS += -march=i586
else
M68K = $(shell which m68k-amigaos-gcc 2>/dev/null)
ifneq ($(M68K),)
CFLAGS = -Os -fomit-frame-pointer -noixemul -isystem .
CC = m68k-amigaos-gcc
AR = m68k-amigaos-ar 
endif
endif
endif


OBJ = \
	pthread_attr.o \
	pthread_barrier.o \
	pthread_cancel.o \
	pthread_cond.o \
	pthread_core.o \
	pthread_key.o \
	pthread_mutex.o \
	pthread_once.o \
	pthread_rwlock.o \
	pthread_spin.o \
	pthread_thread.o \
	sched.o \
	semaphore.o

all: libpthread.a

libpthread.a: $(OBJ)
	$(AR) rcs $@ $?

clean:
	rm -f $(OBJ)
