# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(cc-name),gcc)
# coreboot sdk
CROSS_COMPILE_X86_DEFAULT:=/opt/coreboot-sdk/bin/i386-elf-
else
# llvm sdk
CROSS_COMPILE_X86_DEFAULT:=
endif

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_x86),\
	$(CROSS_COMPILE_X86_DEFAULT))
