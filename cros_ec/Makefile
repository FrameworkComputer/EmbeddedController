# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LIB_SRCS=\
	lib/ec_console.c

STUB_SRCS=\
	chip_stub/ec_uart.c

all: fakemain

clean:
	rm -f fakemain

fakemain: test/fakemain.c $(LIB_SRCS) $(STUB_SRCS)
	gcc -Wall -I include -o fakemain test/fakemain.c \
	$(LIB_SRCS) $(STUB_SRCS)
