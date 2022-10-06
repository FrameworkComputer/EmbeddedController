# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Andestar v3m architecture core OS files build
#

# CPU specific compilation flags
CFLAGS_CPU+=-march=v3m -Os
LDFLAGS_EXTRA+=-mrelax

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o init.o panic.o task.o switch.o __muldi3.o math.o __builtin.o
core-y+=__divdi3.o __udivdi3.o
core-$(CONFIG_FPU)+=__libsoftfpu.o
