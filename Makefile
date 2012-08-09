# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

BOARD ?= bds

PROJECT?=ec

# output directory for build objects
out?=build/$(BOARD)

include Makefile.toolchain

# Get CHIP name
include board/$(BOARD)/build.mk

# Transform the configuration into make variables
includes=include core/$(CORE)/include $(dirs) $(out)
_tsk_lst:=$(shell echo "CONFIG_TASK_LIST" | $(CPP) -P -Iboard/$(BOARD) -Itest \
	  -D"TASK(n, r, d)=n" -imacros $(PROJECT).tasklist)
_tsk_cfg:=$(foreach t,$(_tsk_lst),CONFIG_TASK_$(t))
_flag_cfg:=$(shell $(CPP) $(CPPFLAGS) -P -dN chip/$(CHIP)/config.h | \
		grep -o "CONFIG_.*") \
	   $(shell $(CPP) $(CPPFLAGS) -P -dN board/$(BOARD)/board.h | \
		grep -o "CONFIG_.*")
$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))
CPPFLAGS+=$(foreach t,$(_tsk_cfg),-D$(t))

# Get build configuration from sub-directories
-include private/build.mk
include board/$(BOARD)/build.mk
include chip/$(CHIP)/build.mk
include core/$(CORE)/build.mk
include common/build.mk
include test/build.mk
include util/build.mk
include util/lock/build.mk

includes+=$(includes-y)

objs_from_dir=$(foreach obj, $($(2)-y), \
	        $(out)/$(1)/$(firstword $($(2)-mock-$(PROJECT)-$(obj)) $(obj)))

# Get all sources to build
all-y=$(call objs_from_dir,core/$(CORE),core)
all-y+=$(call objs_from_dir,chip/$(CHIP),chip)
all-y+=$(call objs_from_dir,board/$(BOARD),board)
all-y+=$(call objs_from_dir,private,private)
all-y+=$(call objs_from_dir,common,common)
all-y+=$(call objs_from_dir,test,$(PROJECT))
all-y+=$(call objs_from_dir,vboot,vboot)
dirs=core/$(CORE) chip/$(CHIP) board/$(BOARD) private common test util

include Makefile.rules
