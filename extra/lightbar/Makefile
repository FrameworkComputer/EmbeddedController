# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PROG= lightbar
HEADERS= simulation.h
SRCS= main.c windows.c input.c ../../common/lightbar.c

# comment this out if you don't have libreadline installed
HAS_GNU_READLINE=1

INCLUDE= -I. -I../../include
CFLAGS= -g -Wall -Werror -pthread ${INCLUDE} -DLIGHTBAR_SIMULATION
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
