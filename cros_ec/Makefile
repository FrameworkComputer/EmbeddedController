# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LIB_SRCS=\
	lib/ec_console.c

STUB_SRCS=\
	chip_stub/ec_os.c \
	chip_stub/ec_uart.c

TESTPROGS=fakemain ec_os_test

CFLAGS=-Wall -I include -I chip_stub -pthread

all: $(TESTPROGS)

clean:
	rm -f $(TESTPROGS)

ec_os_test: test/ec_os_test.c chip_stub/ec_os.c chip_stub/ec_uart.c
	gcc $(CFLAGS) -o ec_os_test \
	test/ec_os_test.c chip_stub/ec_os.c chip_stub/ec_uart.c

fakemain: test/fakemain.c $(LIB_SRCS) $(STUB_SRCS)
	gcc $(CFLAGS) -o fakemain test/fakemain.c \
	$(LIB_SRCS) $(STUB_SRCS)
