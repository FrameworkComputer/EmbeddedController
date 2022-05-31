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
# Enable the 'M' extension if config option of RISCV_EXTENSION_M is enabled.
_M_EXTENSION=$(if $(CONFIG_RISCV_EXTENSION_M),m,)
# CPU specific compilation flags
CFLAGS_CPU+=-march=rv32i$(_M_EXTENSION)a$(_FPU_EXTENSION)c
CFLAGS_CPU+=-mabi=ilp32$(_FPU_EXTENSION) -Os
# RISC-V does not trap division by zero, enable the sanitizer to check those.
# With `-fsanitize-undefined-trap-on-error`, we lose a bit of specificity on the
# exact issue, but the added code is as small as it gets.
CFLAGS_CPU+=-fsanitize=integer-divide-by-zero -fsanitize-undefined-trap-on-error
LDFLAGS_EXTRA+=-mrelax
LDFLAGS_EXTRA+=-static-libgcc -lgcc

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o init.o panic.o task.o switch.o __builtin.o math.o
