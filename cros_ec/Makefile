# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CROS_EC_TOP := $(shell pwd)
LIBDIR = $(CROS_EC_TOP)/lib
STUBDIR = $(CROS_EC_TOP)/chip_stub
TESTDIR = $(CROS_EC_TOP)/test
BUILD_ROOT := ${BUILD}/$(shell basename ${CROS_EC_TOP})
LIBS = $(CROS_EC_LIB)  # Firmware library must be self-contained

INCLUDES = \
	-I$(CROS_EC_TOP)/include \
	-I$(LIBDIR)/include

ifeq ($(FIRMWARE_ARCH),)
INCLUDES += -I$(STUBDIR)/include
else
INCLUDES += -I$(FWDIR)/arch/$(FIRMWARE_ARCH)/include
endif

# find ./lib -iname '*.c' | sort
LIB_SRCS = \
	./lib/ec_console.c \
	./lib/ec_host_command.c

LIB_OBJS = $(LIB_SRCS:%.c=${BUILD_ROOT}/%.o)

STUB_SRCS = \
	./chip_stub/ec_os.c \
	./chip_stub/ec_uart.c

STUB_OBJS = $(STUB_SRCS:%.c=${BUILD_ROOT}/%.o)

ALL_SRCS = ${LIB_SRCS} ${STUB_SRCS}

ifeq ($(FIRMWARE_ARCH),)
all : $(CROS_EC_LIB) $(CHIP_STUB_LIB)
else
all : $(CROS_EC_LIB)
endif

include ../common.mk

$(CROS_EC_LIB) : $(LIB_OBJS)
	rm -f $@
	ar qc $@ $^

$(CHIP_STUB_LIB) : $(STUB_OBJS)
	rm -f $@
	ar qc $@ $^
