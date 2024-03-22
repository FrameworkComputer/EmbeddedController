# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC ?= gcc
PKG_CONFIG ?= pkg-config
PROGRAM := touchpad_updater
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
LIBS    += $(shell $(PKG_CONFIG) --libs   libusb-1.0)
CFLAGS  += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
CFLAGS  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

$(PROGRAM): $(SOURCE) Makefile
	$(CC) $(CFLAGS) $(SOURCE) $(LFLAGS) $(LIBS) -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAM) *~
