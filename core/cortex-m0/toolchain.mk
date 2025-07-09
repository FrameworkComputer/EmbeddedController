# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(CROSS_COMPILE_CC_NAME),clang)
# llvm sdk
CROSS_COMPILE_ARM_DEFAULT:=arm-none-eabi-
else
# coreboot sdk
CROSS_COMPILE_ARM_DEFAULT:=arm
COREBOOT_TOOLCHAIN:=arm
USE_COREBOOT_SDK:=1
endif

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	$(CROSS_COMPILE_ARM_DEFAULT))
