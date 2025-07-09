# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CROSS_COMPILE_HOST_DEFAULT:=x86_64-pc-linux-gnu-
CROSS_COMPILE_CC_NAME?=clang
CMAKE_SYSTEM_PROCESSOR ?= x86_64
OPENSSL_NO_ASM ?= 0

$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_host),\
	$(CROSS_COMPILE_HOST_DEFAULT))
