# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PROG= lightbar
HEADERS= simulation.h
SRCS= main.c windows.c input.c ../../common/lightbar.c

# comment this out if you don't have libreadline installed
HAS_GNU_READLINE=1

INCLUDE= -I. -I../../include
CFLAGS= -g -Wall -Werror -pthread ${INCLUDE} -DLIGHTBAR_SIMULATION
CFLAGS+= -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LDFLAGS = -lX11 -lxcb -lrt

ifneq ($(HAS_GNU_READLINE),)
CFLAGS += -DHAS_GNU_READLINE
LDFLAGS += -lreadline
endif

all: ${PROG}

${PROG} : ${SRCS} ${HEADERS} Makefile
	gcc ${CFLAGS} ${SRCS} ${LDFLAGS} -o ${PROG}

.PHONY: clean
clean:
	rm -f ${PROG}
