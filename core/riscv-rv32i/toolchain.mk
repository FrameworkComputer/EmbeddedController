# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(CROSS_COMPILE_CC_NAME),clang)
# llvm sdk
CROSS_COMPILE_RISCV_DEFAULT:=riscv32-cros-elf-
else
# coreboot sdk
CROSS_COMPILE_RISCV_DEFAULT:=riscv64-elf
COREBOOT_TOOLCHAIN:=riscv
USE_COREBOOT_SDK:=1
endif

# Select RISC-V bare-metal toolchain
$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_riscv),\
	$(CROSS_COMPILE_RISCV_DEFAULT))
