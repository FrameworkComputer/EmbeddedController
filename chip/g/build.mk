# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o jtag.o system.o uart.o
