# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CURRENT_DIR:=$(dir $(abspath $(firstword $(MAKEFILE_LIST))))
CFLAGS=-Wall -Werror -I$(CURRENT_DIR) -li2c -lgpiod
SUBDIRS=test

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

um_ppm: rts5453.o rts5453_usermode.o smbus_usermode.o um_ppm_chardev.o platform_usermode.o main.o ppm_common.o
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean local_tests

local_tests:
	$(MAKE) -C test run_local_tests

clean:
	rm -f *.o um_ppm
	$(MAKE) -C test clean
