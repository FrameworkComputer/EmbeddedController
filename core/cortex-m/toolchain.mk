# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(cc-name),gcc)
# coreboot sdk
CROSS_COMPILE_ARM_DEFAULT:=/opt/coreboot-sdk/bin/arm-eabi-
else
# llvm sdk
CROSS_COMPILE_ARM_DEFAULT:=armv7m-cros-eabi-
endif

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	$(CROSS_COMPILE_ARM_DEFAULT))
