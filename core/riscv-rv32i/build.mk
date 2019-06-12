# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# RISC-V core OS files build
#

# Select RISC-V bare-metal toolchain
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_riscv),\
	/opt/coreboot-sdk/bin/riscv64-elf-)

# CPU specific compilation flags
CFLAGS_CPU+=-march=rv32imafc -mabi=ilp32f -Os
LDFLAGS_EXTRA+=-mrelax

core-y=cpu.o init.o panic.o task.o switch.o __builtin.o
