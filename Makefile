# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

# If we came here via a symlink we restart make and deduce the correct BOARD
# value from the current directory.
SYMLINK := $(shell readlink $(lastword $(MAKEFILE_LIST)))

ifneq (,$(SYMLINK))

.PHONY: restart

restart: .DEFAULT
	@true

.DEFAULT:
	@$(MAKE) -C $(dir $(SYMLINK)) \
		--no-print-directory \
		$(MAKECMDGOALS) \
		BOARD=$(notdir $(shell pwd))
else

BOARD ?= bds

PROJECT?=ec

# Output directory for build objects
out?=build/$(BOARD)

# If no key file is provided, use the default dev key
PEM ?= board/$(BOARD)/dev_key.pem

include Makefile.toolchain

# Define the traditional first target. The dependencies of this are near the bottom
# as they can be altered by chip and board files.
all:
.PHONY: all

# The board makefile sets $CHIP and the chip makefile sets $CORE.
# Include those now, since they must be defined for _flag_cfg below.
include board/$(BOARD)/build.mk
include chip/$(CHIP)/build.mk

# Create uppercase config variants, to avoid mixed case constants.
# Also translate '-' to '_', so 'cortex-m' turns into 'CORTEX_M'.  This must
# be done before evaluating config.h.
uppercase = $(shell echo $(1) | tr '[:lower:]-' '[:upper:]_')
UC_BOARD:=$(call uppercase,$(BOARD))
UC_CHIP:=$(call uppercase,$(CHIP))
UC_CHIP_FAMILY:=$(call uppercase,$(CHIP_FAMILY))
UC_CHIP_VARIANT:=$(call uppercase,$(CHIP_VARIANT))
UC_CORE:=$(call uppercase,$(CORE))
UC_PROJECT:=$(call uppercase,$(PROJECT))

# Transform the configuration into make variables.  This must be done after
# the board/project/chip/core variables are defined, since some of the configs
# are dependent on particular configurations.
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
	include/config.h | grep -o "\#define CONFIG_[A-Z0-9_]*" | \
	cut -c9- | sort)

$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))

ifneq "$(CONFIG_COMMON_RUNTIME)" "y"
	_irq_list:=$(shell $(CPP) $(CPPFLAGS) -P -Ichip/$(CHIP) -Iboard/$(BOARD) \
		-D"ENABLE_IRQ(x)=EN_IRQ x" -imacros chip/$(CHIP)/registers.h \
		board/$(BOARD)/ec.irqlist | grep "EN_IRQ .*" | cut -c8-)
	CPPFLAGS+=$(foreach irq,$(_irq_list),\
		    -D"irq_$(irq)_handler_optional=irq_$(irq)_handler")
endif

# Compute RW firmware size and offset
_rw_off_str:=$(shell echo "CONFIG_FW_RW_OFF" | $(CPP) $(CPPFLAGS) -P \
		-Ichip/$(CHIP) -Iboard/$(BOARD) -imacros include/config.h)
_rw_off:=$(shell echo "$$(($(_rw_off_str)))")
_rw_size_str:=$(shell echo "CONFIG_FW_RW_SIZE" | $(CPP) $(CPPFLAGS) -P \
		-Ichip/$(CHIP) -Iboard/$(BOARD) -imacros include/config.h)
_rw_size:=$(shell echo "$$(($(_rw_size_str)))")

$(eval BOARD_$(UC_BOARD)=y)
$(eval CHIP_$(UC_CHIP)=y)
$(eval CHIP_VARIANT_$(UC_CHIP_VARIANT)=y)
$(eval CHIP_FAMILY_$(UC_CHIP_FAMILY)=y)

# Get build configuration from sub-directories
# Note that this re-includes the board and chip makefiles
include board/$(BOARD)/build.mk
include chip/$(CHIP)/build.mk
include core/$(CORE)/build.mk

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

all: $(out)/$(PROJECT).bin utils ${PROJECT_EXTRA}

include Makefile.rules

endif # SYMLINK
