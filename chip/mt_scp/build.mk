# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# SCP specific files build
#

# Required chip modules
chip-y=

ifeq ($(CHIP_VARIANT),mt8183)
CPPFLAGS+=-Ichip/$(CHIP)/$(CHIP_VARIANT)
dirs-y+=chip/$(CHIP)/$(CHIP_VARIANT)
include chip/$(CHIP)/$(CHIP_VARIANT)/build.mk
endif
