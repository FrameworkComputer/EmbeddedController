# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# LLVM does not support Andes architecture.
CROSS_COMPILE_CC_NAME:=gcc

# Set coreboot-sdk as the default toolchain for nds32
NDS32_DEFAULT_COMPILE=/opt/coreboot-sdk/bin/nds32le-elf-

# Select Andes bare-metal toolchain
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_nds32),$(NDS32_DEFAULT_COMPILE))
