# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

# Allow for masking of some targets based on the build architecture. When
# building using a portage package (such as chromeos-ec), this variable will
# already be set. To support the typical developer workflow a default value is
# provided matching the typical architecture of developer workstations. Note
# that amd64 represents the entire x84_64 architecture including intel CPUs.
# This is used to exclude build targets that depend on sanitizers on
# architectures that don't support sanitizers yet (e.g. arm).
ARCH?=amd64
BOARD ?= host

# Directory where the board is configured (includes /$(BOARD) at the end)
BDIR:=$(wildcard board/$(BOARD))

# We need board directory,.
ifeq (,$(BDIR))
$(error unable to locate BOARD $(BOARD))
endif

PROJECT?=ec

# An empty string.
# "-DMACRO" leads to MACRO=1.  Define an empty string "-DMACRO=" to take
# advantage of IS_ENABLED magic macro, which only allows an empty string.
EMPTY=

# Output directory for build objects
out?=build/$(BOARD)

# File containing configuration information
config=$(out)/.config

include Makefile.toolchain

# Define the traditional first target. The dependencies of this are near the
# bottom as they can be altered by chip and board files.
.PHONY: all
all:
	@echo "make all is deprecated"

# Returns the opposite of a configuration variable
# y  ->
# ro -> rw
# rw -> ro
#    -> y
# usage: common-$(call not_cfg,$(CONFIG_FOO))+=bar.o
not_cfg = $(notdir $(filter $(strip $(1))/%, y/ ro/rw rw/ro /y))

# Returns the logical conjuction of two configuration variables
# usage: common-$(call and_cfg,$(CONFIG_FOO),$(CONFIG_BAR))+=foobar.o
and_cfg = $(notdir $(filter $(strip $(1))_$(strip $(2))/%, \
	y_y/y y_ro/ro y_rw/rw y_/ ro_y/ro ro_ro/ro ro_rw/ ro_/ \
	rw_y/rw rw_ro/ rw_rw/rw rw_/ _y/ _ro/ _rw/ _/))

# Run the given shell command and capture the output, but echo the command
# itself, if V is not 0 or empty.
# Usage: $(call shell_echo,<shell-command>)
shell_echo = $(if $(filter-out 0,$(V)),$(info $(1)))$(shell $(1))

# The board makefile sets $CHIP and the chip makefile sets $CORE.
# Include those now, since they must be defined for _flag_cfg below.
include $(BDIR)/build.mk

ifneq ($(ENV_VARS),)
# Let's make sure $(out)/env_config.h changes if value any of the above
# variables has changed since the prvious make invocation. This in turn will
# make sure that relevant object files are re-built.
current_set = $(foreach env_flag, $(ENV_VARS), $(env_flag)=$($(env_flag)))
$(shell util/env_changed.sh "$(out)/env_config.h" "$(current_set)")
endif

include chip/$(CHIP)/build.mk

# The toolchain must be set before referencing any toolchain-related variables
# (CC, CPP, CXX, etc.) so that the correct toolchain is used. The CORE variable
# is set in the CHIP build file, so this include must come after including the
# CHIP build file.
include core/$(CORE)/toolchain.mk

-include build/Makefile.sdk

CROSS_COMPILE_TARGET_arm:=arm-eabi
CROSS_COMPILE_TARGET_riscv:=riscv64-elf
CROSS_COMPILE_TARGET_x86:=i386-elf
CROSS_COMPILE_TARGET_nds32:=nds32le-elf

CROSS_COMPILE_TOOLCHAIN:=$(CROSS_COMPILE_TARGET_$(COREBOOT_TOOLCHAIN))
CROSS_COREBOOT:=$(CROSS_COMPILE_TARGET_$(COREBOOT_TOOLCHAIN))

ifeq (riscv,$(COREBOOT_TOOLCHAIN))
CROSS_COMPILE_TOOLCHAIN:=riscv-elf
endif
ifneq (,$(COREBOOT_SDK_ROOT_$(COREBOOT_TOOLCHAIN)))
CROSS_COMPILE:=$(COREBOOT_SDK_ROOT_$(COREBOOT_TOOLCHAIN))/bin/$(CROSS_COREBOOT)-
else
ifneq (,$(USE_COREBOOT_SDK))
SDK_SCRIPT := util/coreboot_sdk.py
SDK_FLAGS := --toolchain $(CROSS_COMPILE_TOOLCHAIN)
SDK_COMMAND := $(SDK_SCRIPT) $(SDK_FLAGS)
PYTHON_RESULT:=$(shell $(SDK_COMMAND); echo $$?)
CROSS_COMPILE:=$(word 1,$(PYTHON_RESULT))/bin/$(CROSS_COREBOOT)-
EXIT_CODE := $(word 2,$(PYTHON_RESULT))
ifneq ($(EXIT_CODE),0)
CROSS_COMPILE:=/opt/coreboot-sdk/bin/$(CROSS_COREBOOT)-
endif
endif
endif

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
# the board/project/chip/core variables are defined, since some of
# the configs are dependent on particular configurations.
includes=include core/$(CORE)/include include/driver $(dirs) $(out) test \
	third_party
ifeq "$(TEST_BUILD)" "y"
	_tsk_lst_file:=ec.tasklist
	_tsk_lst_flags:=-Itest -DTEST_BUILD=$(EMPTY) \
			-imacros $(PROJECT).tasklist
else
	_tsk_lst_file:=$(PROJECT).tasklist
	_tsk_lst_flags:=
endif

_tsk_lst_flags+=-I$(BDIR) -DBOARD_$(UC_BOARD)=$(EMPTY) \
		-D_MAKEFILE=$(EMPTY) -imacros $(_tsk_lst_file)

_tsk_lst_ro:=$(call shell_echo,$(CPP) $(CPPFLAGS) -P -DSECTION_IS_RO=$(EMPTY) \
	$(_tsk_lst_flags) include/task_filter.h)
_tsk_lst_rw:=$(call shell_echo,$(CPP) $(CPPFLAGS) -P -DSECTION_IS_RW=$(EMPTY) \
	$(_tsk_lst_flags) include/task_filter.h)

_tsk_cfg_ro:=$(foreach t,$(_tsk_lst_ro) ,HAS_TASK_$(t))
_tsk_cfg_rw:=$(foreach t,$(_tsk_lst_rw) ,HAS_TASK_$(t))

_tsk_cfg:= $(filter $(_tsk_cfg_ro), $(_tsk_cfg_rw))
_tsk_cfg_ro:= $(filter-out $(_tsk_cfg), $(_tsk_cfg_ro))
_tsk_cfg_rw:= $(filter-out $(_tsk_cfg), $(_tsk_cfg_rw))

CPPFLAGS_RO+=$(foreach t,$(_tsk_cfg_ro),-D$(t)=$(EMPTY)) \
		$(foreach t,$(_tsk_cfg_rw),-D$(t)_RW=$(EMPTY))
CPPFLAGS_RW+=$(foreach t,$(_tsk_cfg_rw),-D$(t)=$(EMPTY)) \
		$(foreach t,$(_tsk_cfg_ro),-D$(t)_RO=$(EMPTY))
CPPFLAGS+=$(foreach t,$(_tsk_cfg),-D$(t)=$(EMPTY))
ifneq ($(ENV_VARS),)
CPPFLAGS += -DINCLUDE_ENV_CONFIG=$(EMPTY)
CFLAGS += -I$(realpath $(out))
endif
# Get the CONFIG_ and VARIANT_ options that are defined for this target and make
# them into variables available to this build script
# Usage: $(shell $(call cmd_get_configs,<RO|RW>))
cmd_get_configs = $(CPP) $(foreach BLD,$(1),$(CPPFLAGS)) -P -dM \
	-Ichip/$(CHIP) -I$(BDIR) \
	include/config.h | \
	grep -o "\#define \(CONFIG\|VARIANT\)_[A-Z0-9_]*" | cut -c9- | sort
_flag_cfg_ro:=$(call shell_echo,$(call cmd_get_configs,RO))
_flag_cfg_rw:=$(_tsk_cfg_rw) $(call shell_echo,$(call cmd_get_configs,RW))

_flag_cfg:= $(filter $(_flag_cfg_ro), $(_flag_cfg_rw))
_flag_cfg_ro:= $(filter-out $(_flag_cfg), $(_flag_cfg_ro))
_flag_cfg_rw:= $(filter-out $(_flag_cfg), $(_flag_cfg_rw))

$(foreach c,$(_tsk_cfg_rw) $(_flag_cfg_rw),$(eval $(c)=rw))
$(foreach c,$(_tsk_cfg_ro) $(_flag_cfg_ro),$(eval $(c)=ro))
$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))

# Fetch list of mocks from .mocklist files for tests.
# The following will transform the the list of mocks into
# HAS_MOCK_<NAME> for use in the build systems and CPP,
# similar to task definitions.
_mock_lst_flags := -Itest -DTEST_BUILD=$(EMPTY) \
	-imacros $(PROJECT).mocklist \
	-I$(BDIR) -DBOARD_$(UC_BOARD)=$(EMPTY) \
	-D_MAKEFILE=$(EMPTY)
_mock_file := test/$(PROJECT).mocklist

# If test build and mockfile exists, source the list of
# mocks from mockfile.
_mock_lst :=
ifneq ($(and $(TEST_BUILD),$(wildcard $(_mock_file))),)
	_mock_lst += $(call shell_echo,$(CPP) $(CPPFLAGS) -P $(_mock_lst_flags) \
		include/mock_filter.h)
endif

_mock_cfg := $(foreach t,$(_mock_lst) ,HAS_MOCK_$(t))
CPPFLAGS += $(foreach t,$(_mock_cfg),-D$(t)=$(EMPTY))
$(foreach c,$(_mock_cfg),$(eval $(c)=y))

ifneq ($(CONFIG_COMMON_RUNTIME),y)
ifneq ($(CONFIG_DFU_BOOTMANAGER_MAIN),ro)
	_irq_list:=$(call shell_echo,$(CPP) $(CPPFLAGS) -P -Ichip/$(CHIP) \
		-I$(BDIR) \
		-D"ENABLE_IRQ(x)=EN_IRQ x" \
		-imacros chip/$(CHIP)/registers.h \
		- < $(BDIR)/ec.irqlist | grep "EN_IRQ .*" | cut -c8-)
	CPPFLAGS+=$(foreach irq,$(_irq_list),\
		    -D"irq_$(irq)_handler_optional=irq_$(irq)_handler")
endif
endif

# Compute RW firmware size and offset
# Usage: $(shell $(call cmd_config_eval,<CONFIG_*>))
cmd_config_eval = echo "$(1)" | $(CPP) $(CPPFLAGS) -P \
	-Ichip/$(CHIP) -I$(BDIR) \
	-imacros include/config.h -
_rw_off_str:=$(call shell_echo,$(call cmd_config_eval,CONFIG_RW_MEM_OFF))
_rw_off:=$(shell echo "$$(($(_rw_off_str)))")
_rw_size_str:=$(call shell_echo,$(call cmd_config_eval,CONFIG_RW_SIZE))
_rw_size:=$(shell echo "$$(($(_rw_size_str)))")
_program_memory_base_str:=\
$(call shell_echo,$(call cmd_config_eval,CONFIG_PROGRAM_MEMORY_BASE))
_program_memory_base=$(shell echo "$$(($(_program_memory_base_str)))")

$(eval BOARD_$(UC_BOARD)=y)
$(eval CHIP_$(UC_CHIP)=y)
$(eval CORE_$(UC_CORE)=y)
$(eval CHIP_VARIANT_$(UC_CHIP_VARIANT)=y)
$(eval CHIP_FAMILY_$(UC_CHIP_FAMILY)=y)

# Private subdirectories may call this from their build.mk
# First arg is the path to be prepended to configured *.o files.
# Second arg is the config variable (ie, "FOO" to select with $(FOO-$3)).
# Third arg is the config variable value ("y" for configuration options
#   that are set for both RO and RW, "rw" for RW-only configuration options)
objs_from_dir_p=$(foreach obj, $($(2)-$(3)), $(1)/$(obj))
objs_from_dir=$(call objs_from_dir_p,$(1),$(2),y)

# Usage: $(call vars_from_dir,<dest-var-prefix>,<path>,<src-var-prefix>)
# Collect all objects, includes, and dir declarations from sub-directory
# specific variable names, like private-y.
#
# $(1) is the output variable's base name, where values will be deposited.
# $(2) is path that will be prepended to incoming values.
# $(3) is the input variable's base name, which contains the incoming values.
#
# Example:
#   $(eval $(call vars_from_dir,private,subdir,subdir))
#
#   This would set all private variables private-y/ro/rw, private-incs-y,
#   and private-dirs-y variables from the subdir-* equivalent variables, while
#   prefixing all values with "subdir/".
define vars_from_dir
# Transfer all objects.
$(1)-y  += $(addprefix $(2)/,$($(3)-y))
$(1)-ro += $(addprefix $(2)/,$($(3)-ro))
$(1)-rw += $(addprefix $(2)/,$($(3)-rw))
# Transfer all include directories.
$(1)-incs-y += $(addprefix $(2)/,$($(3)-incs-y))
# Transfer all output directories.
$(1)-dirs-y += $(addprefix $(2)/,$($(3)-dirs-y))
endef

# Get build configuration from sub-directories
# Note that this re-includes the board and chip makefiles


include $(BDIR)/build.mk
ifneq ($(BOARD),host)
ifeq ($(USE_BUILTIN_STDLIB), 1)
include builtin/build.mk
else
include libc/build.mk
endif
endif
include chip/$(CHIP)/build.mk
include core/build.mk
include core/$(CORE)/build.mk
include common/build.mk
include driver/build.mk
include power/build.mk
include test/build.mk
include third_party/build.mk
include util/build.mk
include util/lock/build.mk


ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
include third_party/boringssl/common/build.mk
include crypto/build.mk
endif

# Collect all includes.
includes+=$(includes-y)

# Wrapper for fetching all the sources relevant to this build
# target.
# First arg is "y" indicating sources for all segments,
#   or "rw" indicating sources for rw segment.
define get_sources =
# Get sources to build for this target
all-obj-$(1)+=$(call objs_from_dir_p,core/$(CORE),core,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,chip/$(CHIP),chip,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,$(BDIR),board,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,common,common,$(1))
ifeq ($(USE_BUILTIN_STDLIB), 1)
all-obj-$(1)+=$(call objs_from_dir_p,builtin,builtin,$(1))
else
all-obj-$(1)+=$(call objs_from_dir_p,libc,libc,$(1))
endif
all-obj-$(1)+=$(call objs_from_dir_p,driver,driver,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,power,power,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,test,$(PROJECT),$(1))
ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
all-obj-$(1)+= \
    $(call objs_from_dir_p,third_party/boringssl/common,boringssl,$(1))
all-obj-$(1)+= $(call objs_from_dir_p,crypto,crypto,$(1))
endif
endef

# Get all sources to build
$(eval $(call get_sources,y))
$(eval $(call get_sources,ro))

# The following variables are meant to be initialized in the
# board's build.mk. They will later be appended to in util/build.mk with
# utils that should be generated for all boards.
#
# host-util-bin-y  - Utils for the target platform on top of the EC.
#                    For example, the 32-bit x86 Chromebook.
#
# The util targets added to these variable will pickup extra build objects
# from their optional <util_name>-objs make variable.
#
# See commit bc4c1b4 for more context.
ifeq ($(BOARD),host)
host-utils := $(call objs_from_dir,$(out)/util,host-util-bin)
host-utils-cxx := $(call objs_from_dir,$(out)/util,host-util-bin-cxx)
endif
# Use the util_name with an added .c AND the special <util_name>-objs variable.
build-srcs := $(foreach u,$(build-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
                $(wildcard util/$(u).c)))
host-srcs := $(foreach u,$(host-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
               $(wildcard util/$(u).c)))
host-srcs-cxx := $(foreach u,$(host-util-bin-cxx-y), \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc)))

dirs=core/$(CORE) chip/$(CHIP) $(BDIR) common power test
dirs+=$(shell find common -type d)
dirs+=$(shell find driver -type d)
ifeq ($(USE_BUILTIN_STDLIB), 1)
dirs+=builtin
else
dirs+=libc
endif
ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
dirs+=third_party/boringssl/common
dirs+=crypto
endif
common_dirs=util

ifeq ($(custom-ro_objs-y),)
ro-common-objs := $(sort $(foreach obj, $(all-obj-y), $(out)/RO/$(obj)))
ro-only-objs := $(sort $(foreach obj, $(all-obj-ro), $(out)/RO/$(obj)))
ro-objs := $(sort $(ro-common-objs) $(ro-only-objs))
else
ro-objs := $(sort $(foreach obj, $(custom-ro_objs-y), $(out)/RO/$(obj)))
endif

# Add RW-only sources to build
$(eval $(call get_sources,rw))

rw-common-objs := $(sort $(foreach obj, $(all-obj-y), $(out)/RW/$(obj)))
rw-only-objs := $(sort $(foreach obj, $(all-obj-rw), $(out)/RW/$(obj)))
rw-objs := $(sort $(rw-common-objs) $(rw-only-objs))

# Don't include the shared objects in the RO/RW image if we're enabling
# the shared objects library.
ifeq ($(CONFIG_SHAREDLIB),y)
ro-objs := $(filter-out %_sharedlib.o, $(ro-objs))
endif
ro-deps := $(addsuffix .d, $(ro-objs))
rw-deps := $(addsuffix .d, $(rw-objs))

deps := $(ro-deps) $(rw-deps) $(deps-y)

.PHONY: ro rw
$(config): $(out)/$(PROJECT).bin
	@printf '%s=y\n' $(_tsk_cfg) $(_flag_cfg) > $@

compile-only: $(ro-objs) $(rw-objs)

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
export CROSS_COMPILE CFLAGS CC CPP LD NM AR OBJCOPY OBJDUMP
