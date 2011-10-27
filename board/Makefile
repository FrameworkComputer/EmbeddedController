# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CROS_EC_TOP := $(shell pwd)
BUILD_ROOT := ${BUILD}/$(shell basename ${CROS_EC_TOP})

INCLUDES += \
		-I$(TOP)

ifeq ($(FIRMWARE_ARCH),)
INCLUDES += -I$(STUBDIR)/include
else
INCLUDES += -I$(FWDIR)/arch/$(FIRMWARE_ARCH)/include
endif

BOARD_SRCS = \
	board.c

BOARD_OBJS = $(BOARD_SRCS:%.c=${BUILD_ROOT}/%.o)

ALL_SRCS = ${BOARD_SRCS}

all : $(BOARD_LIB)

include ../common.mk

$(BOARD_LIB) : $(BOARD_OBJS)
	rm -f $@
	ar qc $@ $^
