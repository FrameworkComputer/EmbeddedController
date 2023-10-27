# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use your own libmpsse if you want, but we're going to use the files
# that are part of the Chromium OS trunks_client program.
PLATFORM2 = ../../../../platform2
MPSSE_DIR = $(PLATFORM2)/trunks/ftdi

PROG = test_cmds
SRCS = test_cmds.c $(MPSSE_DIR)/mpsse.c $(MPSSE_DIR)/support.c

CFLAGS = \
	-std=gnu99 \
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

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
CFLAGS += -I../../include -I${MPSSE_DIR} -I${PLATFORM2}

CFLAGS += $(shell pkg-config --cflags libusb-1.0 libftdi1)
LIBS   += $(shell pkg-config --libs   libusb-1.0 libftdi1)

$(PROG): $(SRCS) Makefile
	gcc $(CFLAGS) $(SRCS) $(LDFLAGS) $(LIBS) -o $@

.PHONY: clean
clean:
	rm -rf $(PROG)
