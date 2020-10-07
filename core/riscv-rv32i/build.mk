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

# Enable FPU extension if config option of FPU is enabled.
_FPU_EXTENSION=$(if $(CONFIG_FPU),f,)
# CPU specific compilation flags
CFLAGS_CPU+=-march=rv32ima$(_FPU_EXTENSION)c -mabi=ilp32$(_FPU_EXTENSION) -Os
LDFLAGS_EXTRA+=-mrelax
LDFLAGS_EXTRA+=-static-libgcc -lgcc

core-y=cpu.o init.o panic.o task.o switch.o __builtin.o math.o
