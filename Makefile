# Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

BOARD ?= bds

PROJECT?=ec

# output directory for build objects
out?=build/$(BOARD)

# Get build configuration from sub-directories
include board/$(BOARD)/build.mk
include chip/$(CHIP)/build.mk
include common/build.mk
include test/build.mk
include util/build.mk

objs_from_dir=$(foreach obj,$(2), $(out)/$(1)/$(obj))

# Get all sources to build
all-objs=$(call objs_from_dir,chip/$(CHIP),$(chip-objs))
all-objs+=$(call objs_from_dir,board/$(BOARD),$(board-objs))
all-objs+=$(call objs_from_dir,common,$(common-objs))
all-objs+=$(call objs_from_dir,test,$($(PROJECT)-objs))
dirs=chip/$(CHIP) board/$(BOARD) common test util
includes=include $(dirs)

include Makefile.toolchain
include Makefile.rules
