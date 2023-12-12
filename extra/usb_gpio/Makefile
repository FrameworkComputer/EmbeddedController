# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PROGRAM := usb_gpio
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

CFLAGS  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

#
# Add libusb-1.0 required flags
#
LIBS    += $(shell pkg-config --libs   libusb-1.0)
CFLAGS  += $(shell pkg-config --cflags libusb-1.0)

$(PROGRAM): $(SOURCE) Makefile
	gcc $(CFLAGS) $(SOURCE) $(LFLAGS) $(LIBS) -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAM) *~
