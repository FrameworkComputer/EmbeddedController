# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC ?= gcc
PKG_CONFIG ?= pkg-config
PROGRAM := usb_updater
SOURCE  := $(PROGRAM).c
LIBS    :=
LFLAGS  :=
CFLAGS  := -std=gnu99 \
	-g \
        -Wall \
        -Werror \
        -Wpointer-arith \
        -Wcast-align \
        -Wcast-qual \
        -Wundef \
        -Wsign-compare \
        -Wredundant-decls \
        -Wmissing-declarations

ifeq (DEBUG,)
CFLAGS += -O3
else
CFLAGS += -O0
endif

#
# Add libusb-1.0 required flags
#
LIBS    += $(shell $(PKG_CONFIG) --libs   libusb-1.0 libcrypto)
CFLAGS  += $(shell $(PKG_CONFIG) --cflags libusb-1.0 libcrypto)

# NOTE: This may be board-specific
BOARD ?= cr50
CFLAGS  += -I../../include -I../../board/$(BOARD) -I ../../chip/g -I../../util

$(PROGRAM): $(SOURCE) Makefile
	$(CC) $(CFLAGS) $(SOURCE) $(LFLAGS) $(LIBS) -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAM) *~
