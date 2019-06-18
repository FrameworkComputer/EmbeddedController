# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Andestar v3m architecture core OS files build
#

# Set coreboot-sdk as the default toolchain for nds32
NDS32_DEFAULT_COMPILE=/opt/coreboot-sdk/bin/nds32le-elf-

# Select Andes bare-metal toolchain
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_nds32),$(NDS32_DEFAULT_COMPILE))

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
