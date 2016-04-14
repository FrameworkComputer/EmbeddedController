# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PROGRAM := usb_updater
SOURCE  := $(PROGRAM).c
LIBS    :=
LFLAGS  :=
CFLAGS  := -std=gnu99 \
	-g3 \
	-O3 \
        -Wall \
        -Werror \
        -Wpointer-arith \
        -Wcast-align \
        -Wcast-qual \
        -Wundef \
        -Wsign-compare \
        -Wredundant-decls \
        -Wmissing-declarations

#
# Add libusb-1.0 required flags
#
LIBS    += $(shell pkg-config --libs   libusb-1.0)  -lcrypto
CFLAGS  += $(shell pkg-config --cflags libusb-1.0)

# NOTE: This may be board-specific
BOARD ?= cr50
CFLAGS  += -I../../include -I../../board/$(BOARD) -I ../../chip/g -I../../util

$(PROGRAM): $(SOURCE) Makefile
	gcc $(CFLAGS) $(SOURCE) $(LFLAGS) $(LIBS) -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAM) *~
