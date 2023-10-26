# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC ?= gcc
PKG_CONFIG ?= pkg-config
PROGRAMS := usb_updater2
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

ifneq ($(DEBUG),)
CFLAGS += -O0
else
CFLAGS += -O3
endif

#
# Add libusb-1.0 required flags
#
LIBS    += $(shell $(PKG_CONFIG) --libs   libusb-1.0)
CFLAGS  += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
CFLAGS  += -I../../include -I../../util -I../../fuzz -I../../test
CFLAGS  += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

VPATH = ../../util

LIBS_common  = -lfmap

all: $(PROGRAMS)

%.o: %.c
	$(CC) $(CFLAGS) -c -MMD -MF $(basename $@).d -o $@ $<

# common EC code USB updater
usb_updater2: usb_updater2.c Makefile
	$(CC) $(CFLAGS) $< $(LFLAGS) $(LIBS) $(LIBS_common) -o $@

.PHONY: clean

clean:
	rm -rf $(PROGRAMS) *~ *.o *.d dp

parser_debug: desc_parser.c
	gcc -g -O0  -DTEST_PARSER desc_parser.c -o dp
