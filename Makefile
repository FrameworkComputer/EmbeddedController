# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

export FIRMWARE_ARCH

export CC ?= gcc
export CXX ?= g++
export CFLAGS = -Wall -Werror

ifeq (${DEBUG},)
CFLAGS += -O3
else
CFLAGS += -O0 -g
endif

# Fix compiling directly on host (outside of emake)
ifeq ($(ARCH),)
export ARCH=amd64
endif

ifneq (${DEBUG},)
CFLAGS += -DVBOOT_DEBUG
endif

ifeq (${DISABLE_NDEBUG},)
CFLAGS += -DNDEBUG
endif

export TOP = $(shell pwd)
export CROS_EC_DIR=$(TOP)/cros_ec
export CHIP_STUB_DIR=$(CROS_EC_DIR)/chip_stub
export BOARD_DIR=$(TOP)/board

INCLUDES = -I$(TOP)/chip_interface -I$(CROS_EC_DIR)/include

ifeq ($(FIRMWARE_ARCH),)
INCLUDES += -I$(CHIP_STUB_DIR)/include
endif

export INCLUDES

export BUILD = ${TOP}/build
export CROS_EC_LIB = ${BUILD}/cros_ec.a
export CHIP_STUB_LIB = ${BUILD}/chip_stub.a
export BOARD_LIB = ${BUILD}/board.a

ifeq ($(FIRMWARE_ARCH),)
SUBDIRS = board cros_ec cros_ec/test utility
else
SUBDIRS = board cros_ec
endif

all:
	set -e; \
	for d in $(shell find ${SUBDIRS} -name '*.c' -exec  dirname {} \; |\
		 sort -u); do \
		newdir=${BUILD}/$$d; \
		if [ ! -d $$newdir ]; then \
			mkdir -p $$newdir; \
		fi; \
	done; \
	for i in $(SUBDIRS); do \
		make -C $$i; \
	done

clean:
	/bin/rm -rf ${BUILD}

install:
	$(MAKE) -C utility install

runtests:
	$(MAKE) -C cros_ec/test runtests
