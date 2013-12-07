# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

BOARD ?= bds

PROJECT?=ec

# Output directory for build objects
out?=build/$(BOARD)

include Makefile.toolchain

# The board makefile sets $CHIP.  Include it now, since it must be
# defined for _flag_cfg below.
include board/$(BOARD)/build.mk

# Transform the configuration into make variables
includes=include core/$(CORE)/include $(dirs) $(out) test
ifeq "$(TEST_BUILD)" "y"
	_tsk_lst:=$(shell echo "CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST" | \
		    $(CPP) -P -Iboard/$(BOARD) -Itest \
		    -D"TASK_NOTEST(n, r, d, s)=" -D"TASK_ALWAYS(n, r, d, s)=n" \
		    -D"TASK_TEST(n, r, d, s)=n" -imacros ec.tasklist \
		    -imacros $(PROJECT).tasklist)
else
	_tsk_lst:=$(shell echo "CONFIG_TASK_LIST" | $(CPP) -P \
		    -Iboard/$(BOARD) -D"TASK_NOTEST(n, r, d, s)=n" \
		    -D"TASK_ALWAYS(n, r, d, s)=n" -imacros ec.tasklist)
endif
_tsk_cfg:=$(foreach t,$(_tsk_lst) ,HAS_TASK_$(t))
CPPFLAGS+=$(foreach t,$(_tsk_cfg),-D$(t))
_flag_cfg:=$(shell $(CPP) $(CPPFLAGS) -P -dM -Ichip/$(CHIP) -Iboard/$(BOARD) \
	include/config.h | grep -o "\#define CONFIG_[A-Za-z0-9_]*" | \
	cut -c9- | sort)

$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))

# Get build configuration from sub-directories
# Note that this re-includes the board makefile

include board/$(BOARD)/build.mk
include chip/$(CHIP)/build.mk
include core/$(CORE)/build.mk

# Create uppercase config variants, to avoid mixed case constants.
# Also translate '-' to '_', so 'cortex-m' turns into 'CORTEX_M'.
# This must be done after including board/chip/core configs, since we
# want to run 'tr' once per variable instead of once per reference.
uppercase = $(shell echo $(1) | tr '[:lower:]-' '[:upper:]_')
UC_BOARD:=$(call uppercase,$(BOARD))
UC_CHIP:=$(call uppercase,$(CHIP))
UC_CHIP_FAMILY:=$(call uppercase,$(CHIP_FAMILY))
UC_CHIP_VARIANT:=$(call uppercase,$(CHIP_VARIANT))
UC_CORE:=$(call uppercase,$(CORE))

$(eval BOARD_$(UC_BOARD)=y)

include common/build.mk
include driver/build.mk
include power/build.mk
-include private/build.mk
include test/build.mk
include util/build.mk
include util/lock/build.mk

includes+=$(includes-y)

objs_from_dir=$(sort $(foreach obj, $($(2)-y), \
	        $(out)/$(1)/$(firstword $($(2)-mock-$(PROJECT)-$(obj)) $(obj))))

# Get all sources to build
all-y=$(call objs_from_dir,core/$(CORE),core)
all-y+=$(call objs_from_dir,chip/$(CHIP),chip)
all-y+=$(call objs_from_dir,board/$(BOARD),board)
all-y+=$(call objs_from_dir,private,private)
all-y+=$(call objs_from_dir,common,common)
all-y+=$(call objs_from_dir,driver,driver)
all-y+=$(call objs_from_dir,power,power)
all-y+=$(call objs_from_dir,test,$(PROJECT))
dirs=core/$(CORE) chip/$(CHIP) board/$(BOARD) private common power test util
dirs+=$(shell find driver -type d)

include Makefile.rules
