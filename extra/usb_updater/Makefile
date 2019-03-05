# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CC ?= gcc
PKG_CONFIG ?= pkg-config
PROGRAMS := gsctool usb_updater2
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
LIBS    += $(shell $(PKG_CONFIG) --libs   libusb-1.0)
CFLAGS  += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
CFLAGS  += -I../../include -I../../util -I../../fuzz -I../../test

VPATH = ../../util

BOARD := cr50
LIBS_g  = $(shell $(PKG_CONFIG) --libs   libcrypto)
CFLAGS_g = $(shell $(PKG_CONFIG) --cflags libcrypto)
CFLAGS_g  += -I../../board/cr50 -I ../../chip/g

LIBS_common  = -lfmap

all: $(PROGRAMS)

GSCTOOL_SOURCES := gsctool.c desc_parser.c usb_if.c verify_ro.c
GSCTOOL_OBJS := $(patsubst %.c,%.o,$(GSCTOOL_SOURCES))
DEPS := $(patsubst %.c,%.d,$(GSCTOOL_SOURCES))

# chip/g updater
gsctool: $(GSCTOOL_OBJS) Makefile
	$(CC) $(GSCTOOL_OBJS) $(LFLAGS) $(LIBS) $(LIBS_g) -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_g) -c -MMD -MF $(basename $@).d -o $@ $<

gsctool.o:  generated_version.h

# common EC code USB updater
usb_updater2: usb_updater2.c Makefile
	$(CC) $(CFLAGS) $< $(LFLAGS) $(LIBS) $(LIBS_common) -o $@

.PHONY: clean

generated_version.h: $(GSCTOOL_SOURCES)
	# Make sure ${BOARD} not set to anything when invoking getversion.sh,
	# as even when building with Cr50 enabled, other directories do not
	# matter for gsctool.
	@(cd ../../; BOARD= util/getversion.sh) > $@

clean:
	rm -rf $(PROGRAMS) *~ *.o *.d dp generated_version.h

parser_debug: desc_parser.c
	gcc -g -O0  -DTEST_PARSER desc_parser.c -o dp

-include $(DEPS)
