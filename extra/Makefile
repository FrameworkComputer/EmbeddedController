# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PROG= lightbar
HEADERS= simulation.h
SRCS= main.c windows.c input.c ../common/lightbar.c

INCLUDE= -I. -I../include
CFLAGS= -g -Wall -Werror -pthread ${INCLUDE} -DLIGHTBAR_SIMULATION
LDFLAGS = -lX11 -lxcb -lrt

all: ${PROG}

${PROG} : ${SRCS} ${HEADERS} Makefile
	gcc ${CFLAGS} ${SRCS} ${LDFLAGS} -o ${PROG}

.PHONY: clean
clean:
	rm -f ${PROG}
