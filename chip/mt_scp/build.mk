# -*- makefile -*-
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# SCP specific files build
#

# Required chip modules
chip-y=

ifeq ($(CHIP_VARIANT),$(filter $(CHIP_VARIANT),mt8183 mt8186))
CPPFLAGS+=-Ichip/$(CHIP)/mt818x
dirs-y+=chip/$(CHIP)/mt818x
include chip/$(CHIP)/mt818x/build.mk
endif

ifeq ($(CHIP_VARIANT),$(filter $(CHIP_VARIANT),mt8192 mt8195 mt8188))
CPPFLAGS+=-Ichip/$(CHIP)/rv32i_common -Ichip/$(CHIP)/$(CHIP_VARIANT)
dirs-y+=chip/$(CHIP)/rv32i_common chip/$(CHIP)/$(CHIP_VARIANT)
include chip/$(CHIP)/rv32i_common/build.mk
# Each chip variant can provide specific build.mk if any
include chip/$(CHIP)/$(CHIP_VARIANT)/build.mk
endif
