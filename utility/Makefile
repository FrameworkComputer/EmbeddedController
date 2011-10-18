# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

all: ec_uartd

clean:
	rm -f ec_uartd

ec_uartd: ec_uartd.c
	gcc -o ec_uartd -lftdi ec_uartd.c
