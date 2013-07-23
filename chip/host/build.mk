# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

CORE:=host

chip-y=system.o gpio.o uart.o persistence.o flash.o lpc.o reboot.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
