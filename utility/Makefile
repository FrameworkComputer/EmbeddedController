# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CFLAGS += $(INCLUDES)
CFLAGS += -MMD -MF $@.d
HOSTCC = cc

BUILD_ROOT = ${BUILD}/utility

DESTDIR ?= /usr/bin

TARGET_NAMES = ec_uartd

TARGET_BINS = $(addprefix ${BUILD_ROOT}/,$(TARGET_NAMES))
ALL_DEPS = $(addsuffix .d,${TARGET_BINS})

all: $(TARGET_BINS)

${BUILD_ROOT}/ec_uartd: ec_uartd.c $(LIBS)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS) -lftdi

install: $(TARGET_BINS)
	mkdir -p $(DESTDIR)
	cp -f $(TARGET_BINS) $(DESTDIR)
	chmod a+rx $(patsubst %,$(DESTDIR)/%,$(TARGET_NAMES))

-include ${ALL_DEPS}
