# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(CROSS_COMPILE_CC_NAME),clang)
# llvm sdk
CROSS_COMPILE_ARM_DEFAULT:=armv7m-cros-eabi-
else
# coreboot sdk
CROSS_COMPILE_ARM_DEFAULT:=arm-eabi
COREBOOT_TOOLCHAIN:=arm
USE_COREBOOT_SDK:=1
endif
CMAKE_SYSTEM_PROCESSOR ?= armv7
# TODO(b/275450331): Enable the asm after we fix the crash.
OPENSSL_NO_ASM ?= 1

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	$(CROSS_COMPILE_ARM_DEFAULT))
