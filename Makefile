# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

BOARD ?= bds

# Directory where the board is configured (includes /$(BOARD) at the end)
BDIR:=$(wildcard board/$(BOARD) private-*/board/$(BOARD))
# There can be only one <insert exploding windows here>
ifeq (,$(BDIR))
$(error unable to locate BOARD $(BOARD))
endif
ifneq (1,$(words $(BDIR)))
$(error multiple definitions for BOARD $(BOARD): $(BDIR))
endif
ifneq ($(filter private-%,$(BDIR)),)
PDIR=$(subst /board/$(BOARD),,$(BDIR))
endif

PROJECT?=ec

# Output directory for build objects
ifdef CTS_MODULE
# CTS builds need different directories per board per suite.
out?=build/$(BOARD)/cts_$(CTS_MODULE)
else
out?=build/$(BOARD)
endif

# File containing configuration information
config=$(out)/.config

# If no key file is provided, use the default dev key
PEM ?= $(BDIR)/dev_key.pem

include Makefile.toolchain

# Define the traditional first target. The dependencies of this are near the
# bottom as they can be altered by chip and board files.
.PHONY: all
all:

# The board makefile sets $CHIP and the chip makefile sets $CORE.
# Include those now, since they must be defined for _flag_cfg below.
include $(BDIR)/build.mk
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
ifdef CTS_MODULE
includes+=cts/$(CTS_MODULE) cts
endif
ifeq "$(TEST_BUILD)" "y"
	_tsk_lst_file:=ec.tasklist
	_tsk_lst:=$(shell echo "CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST" | \
		    $(CPP) -P -I$(BDIR) -DBOARD_$(UC_BOARD) -Itest \
		    -D"TASK_NOTEST(n, r, d, s)=" -D"TASK_ALWAYS(n, r, d, s)=n" \
		    -D"TASK_TEST(n, r, d, s)=n" -imacros $(_tsk_lst_file) \
		    -imacros $(PROJECT).tasklist)
else ifdef CTS_MODULE
	_tsk_lst_file:=ec.tasklist
	_tsk_lst:=$(shell echo "CONFIG_TASK_LIST CONFIG_CTS_TASK_LIST" | \
		    $(CPP) -P -I cts/$(CTS_MODULE) -Icts -I$(BDIR) \
		    -DBOARD_$(UC_BOARD) \
		    -D"TASK_NOTEST(n, r, d, s)=n" \
		    -D"TASK_ALWAYS(n, r, d, s)=n" \
		    -imacros $(_tsk_lst_file) \
		    -imacros cts.tasklist)
else
	_tsk_lst_file:=$(PROJECT).tasklist
	_tsk_lst:=$(shell echo "CONFIG_TASK_LIST" | $(CPP) -P \
		    -I$(BDIR) -DBOARD_$(UC_BOARD) \
		    -D"TASK_NOTEST(n, r, d, s)=n" \
		    -D"TASK_ALWAYS(n, r, d, s)=n" -imacros $(_tsk_lst_file))
endif

_tsk_cfg:=$(foreach t,$(_tsk_lst) ,HAS_TASK_$(t))
CPPFLAGS+=$(foreach t,$(_tsk_cfg),-D$(t))
_flag_cfg:=$(shell $(CPP) $(CPPFLAGS) -P -dM -Ichip/$(CHIP) -I$(BDIR) \
	include/config.h | grep -o "\#define CONFIG_[A-Z0-9_]*" | \
	cut -c9- | sort)

$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))

ifneq "$(CONFIG_COMMON_RUNTIME)" "y"
	_irq_list:=$(shell $(CPP) $(CPPFLAGS) -P -Ichip/$(CHIP) -I$(BDIR) \
		-D"ENABLE_IRQ(x)=EN_IRQ x" -imacros chip/$(CHIP)/registers.h \
		$(BDIR)/ec.irqlist | grep "EN_IRQ .*" | cut -c8-)
	CPPFLAGS+=$(foreach irq,$(_irq_list),\
		    -D"irq_$(irq)_handler_optional=irq_$(irq)_handler")
endif

# Compute RW firmware size and offset
_rw_off_str:=$(shell echo "CONFIG_RW_MEM_OFF" | $(CPP) $(CPPFLAGS) -P \
		-Ichip/$(CHIP) -I$(BDIR) -imacros include/config.h)
_rw_off:=$(shell echo "$$(($(_rw_off_str)))")
_rw_size_str:=$(shell echo "CONFIG_RW_SIZE" | $(CPP) $(CPPFLAGS) -P \
		-Ichip/$(CHIP) -I$(BDIR) -imacros include/config.h)
_rw_size:=$(shell echo "$$(($(_rw_size_str)))")
_program_memory_base_str:=$(shell echo "CONFIG_PROGRAM_MEMORY_BASE" | \
		$(CPP) $(CPPFLAGS) -P \
		-Ichip/$(CHIP) -I$(BDIR) -imacros include/config.h)
_program_memory_base=$(shell echo "$$(($(_program_memory_base_str)))")

$(eval BOARD_$(UC_BOARD)=y)
$(eval CHIP_$(UC_CHIP)=y)
$(eval CHIP_VARIANT_$(UC_CHIP_VARIANT)=y)
$(eval CHIP_FAMILY_$(UC_CHIP_FAMILY)=y)

# Private subdirectories may call this from their build.mk
# First arg is the path to be prepended to configured *.o files.
# Second arg is the config variable (ie, "FOO" to select with $(FOO-y)).
objs_from_dir=$(foreach obj, $($(2)-y), $(1)/$(obj))

# Get build configuration from sub-directories
# Note that this re-includes the board and chip makefiles

ifdef CTS_MODULE
include cts/build.mk
endif
include $(BDIR)/build.mk
include chip/$(CHIP)/build.mk
include core/$(CORE)/build.mk
include common/build.mk
include driver/build.mk
include power/build.mk
-include private/build.mk
ifneq ($(PDIR),)
include $(PDIR)/build.mk
endif
include test/build.mk
include util/build.mk
include util/lock/build.mk
include util/signer/build.mk

includes+=$(includes-y)

# Get all sources to build
all-obj-y+=$(call objs_from_dir,core/$(CORE),core)
all-obj-y+=$(call objs_from_dir,chip/$(CHIP),chip)
all-obj-y+=$(call objs_from_dir,$(BDIR),board)
all-obj-y+=$(call objs_from_dir,private,private)
ifneq ($(PDIR),)
all-obj-y+=$(call objs_from_dir,$(PDIR),$(PDIR))
endif
all-obj-y+=$(call objs_from_dir,common,common)
all-obj-y+=$(call objs_from_dir,driver,driver)
all-obj-y+=$(call objs_from_dir,power,power)
ifdef CTS_MODULE
all-obj-y+=$(call objs_from_dir,cts,cts)
endif
all-obj-y+=$(call objs_from_dir,test,$(PROJECT))
dirs=core/$(CORE) chip/$(CHIP) $(BDIR) common power test cts/common cts/$(CTS_MODULE)
dirs+= private $(PDIR)
dirs+=$(shell find driver -type d)
common_dirs=util

ifeq ($(custom-ro_objs-y),)
ro-objs := $(sort $(foreach obj, $(all-obj-y), $(out)/RO/$(obj)))
else
ro-objs := $(sort $(foreach obj, $(custom-ro_objs-y), $(out)/RO/$(obj)))
endif

rw-objs := $(sort $(foreach obj, $(all-obj-y), $(out)/RW/$(obj)))

# Don't include the shared objects in the RO/RW image if we're enabling
# the shared objects library.
ifeq ($(CONFIG_SHAREDLIB),y)
ro-objs := $(filter-out %_sharedlib.o, $(ro-objs))
endif
ro-deps := $(ro-objs:%.o=%.o.d)
rw-deps := $(rw-objs:%.o=%.o.d)
deps := $(ro-deps) $(rw-deps)

.PHONY: ro rw
$(config): $(out)/$(PROJECT).bin
	@printf '%s=y\n' $(_tsk_cfg) $(_flag_cfg) > $@

def_all_deps:=utils ro rw notice $(config) $(PROJECT_EXTRA)
all_deps?=$(def_all_deps)
all: $(all_deps)

ro: override BLD:=RO
ro: $(libsharedobjs_elf-y) $(out)/RO/$(PROJECT).RO.flat

rw: override BLD:=RW
rw: $(libsharedobjs_elf-y) $(out)/RW/$(PROJECT).RW.flat

# Shared objects library
SHOBJLIB := libsharedobjs
sharedlib-objs := $(filter %_sharedlib.o, $(all-obj-y))
sharedlib-objs := $(foreach obj, $(sharedlib-objs), $(out)/$(SHOBJLIB)/$(obj))
sharedlib-deps := $(sharedlib-objs:%.o=%.o.d)
deps += $(sharedlib-deps)
def_libsharedobjs_deps := $(sharedlib-objs)
libsharedobjs_deps ?= $(def_libsharedobjs_deps)

libsharedobjs-$(CONFIG_SHAREDLIB) := $(out)/$(SHOBJLIB)/$(SHOBJLIB).flat
libsharedobjs_elf-$(CONFIG_SHAREDLIB) := \
	$(libsharedobjs-$(CONFIG_SHAREDLIB):%.flat=%.elf)
libsharedobjs: $(libsharedobjs-y)

include Makefile.rules
export CROSS_COMPILE CFLAGS CC CPP LD  NM AR OBJCOPY OBJDUMP
