# -*- makefile -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M4 core OS files build
#

# FPU compilation flags
CFLAGS_FPU-$(CONFIG_FPU)=-mfpu=fpv4-sp-d16 -mfloat-abi=hard

# CPU specific compilation flags
CFLAGS_CPU=-mcpu=cortex-m4 -mthumb -Os -mno-sched-prolog
CFLAGS_CPU+=$(CFLAGS_FPU-y)

core-y=cpu.o init.o panic.o switch.o task.o timer.o
core-$(CONFIG_TASK_WATCHDOG)+=watchdog.o
