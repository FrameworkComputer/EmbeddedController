# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M0 core OS files build
#

# Select ARMv6-m compatible bare-metal toolchain
CROSS_COMPILE?=arm-none-eabi-

# CPU specific compilation flags
CFLAGS_CPU+=-mthumb -Os -mno-sched-prolog
CFLAGS_CPU+=-mno-unaligned-access

core-y=cpu.o init.o panic.o switch.o task.o thumb_case.o div.o
core-$(CONFIG_WATCHDOG)+=watchdog.o
