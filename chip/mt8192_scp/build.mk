# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# SCP specific files build
#

CORE:=riscv-rv32i

# Required chip modules
chip-y+=clock.o
chip-y+=gpio.o
chip-y+=intc.o
chip-y+=system.o
chip-y+=uart.o

# Optional chip modules
chip-$(CONFIG_COMMON_TIMER)+=hrtimer.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
