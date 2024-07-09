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
# This is used to exclude build targets that depend on sanitizers such as
# fuzzers on architectures that don't support sanitizers yet (e.g. arm).
ARCH?=amd64
BOARD ?= elm

# Directory where the board is configured (includes /$(BOARD) at the end)
BDIR:=$(wildcard board/$(BOARD))
# Private board directory
# (b/342416995, b/341194123): There's a secret MTK SCP repo and it's not in the
# internal manifest - it needs additional local manifest setup and only limited
# people can access it. No ToT EC CQ is set up for that repo.
# The rule below is reserved for the SCP firmware branches and local builds.
# DO NOT remove PBDIR and PDIR before you read the bug and make sure that's the
# right thing to do.
PBDIR:=$(wildcard ../private-*/board/$(BOARD))

# We need either public, or private board directory, or both.
ifeq (,$(BDIR)$(PBDIR))
$(error unable to locate BOARD $(BOARD))
endif

# Setup PDIR (private directory root).
ifneq (,$(PBDIR))
ifneq (1,$(words $(PBDIR)))
$(error multiple private definitions for BOARD $(BOARD): $(PBDIR))
endif

PDIR:=$(subst /board/$(BOARD),,$(PBDIR))
endif

# If only private is present, use that as BDIR.
ifeq (,$(BDIR))
BDIR:=$(PBDIR)
endif

PROJECT?=ec

# An empty string.
# "-DMACRO" leads to MACRO=1.  Define an empty string "-DMACRO=" to take
# advantage of IS_ENABLED magic macro, which only allows an empty string.
EMPTY=

# Output directory for build objects
ifdef CTS_MODULE
# CTS builds need different directories per board per suite.
out?=build/$(BOARD)/cts_$(CTS_MODULE)
else ifdef TEST_COVERAGE
out?=build/coverage/initial-$(BOARD)
else
out?=build/$(BOARD)
endif

# File containing configuration information
config=$(out)/.config

# If no key file is provided, use the default dev key
PEM ?= $(BDIR)/dev_key.pem

# Port for flash_ec. Defaults to 9999.
PORT ?= 9999

# If CONFIG_TOUCHPAD_HASH_FW is set, include hashes of a touchpad firmware in
# the EC image (if no touchpad firmware is provided, just output blank hashes).
# The TOUCHPAD_FW_DIR parameter is used for building multiple boards as it is
# board-independent.
ifdef TOUCHPAD_FW_DIR
TOUCHPAD_FW ?= $(TOUCHPAD_FW_DIR)/$(BOARD)/touchpad.bin
else
TOUCHPAD_FW ?=
endif

# If TEST_FUZZ is set make sure at least one sanitizer is enabled.
ifeq ($(TEST_FUZZ)_$(TEST_ASAN)$(TEST_MSAN)$(TEST_UBSAN),y_)
override TEST_ASAN:=y
endif

include Makefile.toolchain

# Define the traditional first target. The dependencies of this are near the
# bottom as they can be altered by chip and board files.
.PHONY: all
all:

# Returns the opposite of a configuration variable
# y  ->
# ro -> rw
# rw -> ro
#    -> y
# usage: common-$(call not_cfg,$(CONFIG_FOO))+=bar.o
not_cfg = $(subst ro rw,y,$(filter-out $(1:y=ro rw),ro rw))

# Run the given shell command and capture the output, but echo the command
# itself, if V is not 0 or empty.
# Usage: $(call shell_echo,<shell-command>)
shell_echo = $(if $(filter-out 0,$(V)),$(info $(1)))$(shell $(1))

# Check if private driver repository is present
ec-private:=../ec-private
ifneq ($(wildcard $(ec-private)/build.mk),)
HAVE_PRIVATE:=y
CPPFLAGS+=-DHAVE_PRIVATE
endif

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

# Baseboard directory
ifneq (,$(BASEBOARD))
BASEDIR:=baseboard/$(BASEBOARD)
CFLAGS_BASEBOARD=-DHAS_BASEBOARD=$(EMPTY) -DBASEBOARD_$(UC_BASEBOARD)=$(EMPTY)
include $(BASEDIR)/build.mk
else
# If BASEBOARD is not defined, then assign BASEDIR to BDIR. This avoids
# the need to have so many conditional checks wherever BASEDIR is used
# below.
BASEDIR:=$(BDIR)
CFLAGS_BASEBOARD=
endif
include chip/$(CHIP)/build.mk

# The toolchain must be set before referencing any toolchain-related variables
# (CC, CPP, CXX, etc.) so that the correct toolchain is used. The CORE variable
# is set in the CHIP build file, so this include must come after including the
# CHIP build file.
include core/$(CORE)/toolchain.mk

# Create uppercase config variants, to avoid mixed case constants.
# Also translate '-' to '_', so 'cortex-m' turns into 'CORTEX_M'.  This must
# be done before evaluating config.h.
uppercase = $(shell echo $(1) | tr '[:lower:]-' '[:upper:]_')
UC_BASEBOARD:=$(call uppercase,$(BASEBOARD))
UC_BOARD:=$(call uppercase,$(BOARD))
UC_CHIP:=$(call uppercase,$(CHIP))
UC_CHIP_FAMILY:=$(call uppercase,$(CHIP_FAMILY))
UC_CHIP_VARIANT:=$(call uppercase,$(CHIP_VARIANT))
UC_CORE:=$(call uppercase,$(CORE))
UC_PROJECT:=$(call uppercase,$(PROJECT))

# Transform the configuration into make variables.  This must be done after
# the board/baseboard/project/chip/core variables are defined, since some of
# the configs are dependent on particular configurations.
includes=include core/$(CORE)/include include/driver $(dirs) $(out) fuzz test third_party
ifdef CTS_MODULE
includes+=cts/$(CTS_MODULE) cts
endif
ifeq "$(TEST_BUILD)" "y"
	_tsk_lst_file:=ec.tasklist
	_tsk_lst_flags:=$(if $(TEST_FUZZ),-Ifuzz,-Itest) -DTEST_BUILD=$(EMPTY) \
			-imacros $(PROJECT).tasklist
else ifdef CTS_MODULE
	_tsk_lst_file:=ec.tasklist
	_tsk_lst_flags:=-I cts/$(CTS_MODULE) -Icts -DCTS_MODULE=$(CTS_MODULE) \
			-imacros cts.tasklist
else
	_tsk_lst_file:=$(PROJECT).tasklist
	_tsk_lst_flags:=
endif

_tsk_lst_flags+=-I$(BDIR) -DBOARD_$(UC_BOARD)=$(EMPTY) -I$(BASEDIR) \
		-DBASEBOARD_$(UC_BASEBOARD)=$(EMPTY) \
		-D_MAKEFILE=$(EMPTY) -imacros $(_tsk_lst_file)
-include $(ec-private)/task_list_flags.mk

_tsk_lst_ro:=$(call shell_echo,$(CPP) -P -DSECTION_IS_RO=$(EMPTY) \
	$(_tsk_lst_flags) include/task_filter.h)
_tsk_lst_rw:=$(call shell_echo,$(CPP) -P -DSECTION_IS_RW=$(EMPTY) \
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
	-Ichip/$(CHIP) -I$(BASEDIR) -I$(BDIR) \
	$(if $(HAVE_PRIVATE), -I$(ec-private)) include/config.h | \
	grep -o "\#define \(CONFIG\|VARIANT\)_[A-Z0-9_]*" | cut -c9- | sort
_flag_cfg_ro:=$(call shell_echo,$(call cmd_get_configs,RO))
_flag_cfg_rw:=$(_tsk_cfg_rw) $(call shell_echo,$(call cmd_get_configs,RW))

_flag_cfg:= $(filter $(_flag_cfg_ro), $(_flag_cfg_rw))
_flag_cfg_ro:= $(filter-out $(_flag_cfg), $(_flag_cfg_ro))
_flag_cfg_rw:= $(filter-out $(_flag_cfg), $(_flag_cfg_rw))

$(foreach c,$(_tsk_cfg_rw) $(_flag_cfg_rw),$(eval $(c)=rw))
$(foreach c,$(_tsk_cfg_ro) $(_flag_cfg_ro),$(eval $(c)=ro))
$(foreach c,$(_tsk_cfg) $(_flag_cfg),$(eval $(c)=y))

# Fetch list of mocks from .mocklist files for tests and fuzzers.
# The following will transform the the list of mocks into
# HAS_MOCK_<NAME> for use in the build systems and CPP,
# similar to task definitions.
_mock_lst_flags := $(if $(TEST_FUZZ),-Ifuzz,-Itest) -DTEST_BUILD=$(EMPTY) \
	-imacros $(PROJECT).mocklist                                      \
	-I$(BDIR) -DBOARD_$(UC_BOARD)=$(EMPTY) -I$(BASEDIR)               \
	-DBASEBOARD_$(UC_BASEBOARD)=$(EMPTY)                              \
	-D_MAKEFILE=$(EMPTY)
_mock_file := $(if $(TEST_FUZZ),fuzz,test)/$(PROJECT).mocklist

# If test/fuzz build and mockfile exists, source the list of
# mocks from mockfile.
_mock_lst :=
ifneq ($(and $(TEST_BUILD),$(wildcard $(_mock_file))),)
	_mock_lst += $(call shell_echo,$(CPP) -P $(_mock_lst_flags) \
		include/mock_filter.h)
endif

_mock_cfg := $(foreach t,$(_mock_lst) ,HAS_MOCK_$(t))
CPPFLAGS += $(foreach t,$(_mock_cfg),-D$(t)=$(EMPTY))
$(foreach c,$(_mock_cfg),$(eval $(c)=y))

ifneq ($(CONFIG_COMMON_RUNTIME),y)
ifneq ($(CONFIG_DFU_BOOTMANAGER_MAIN),ro)
	_irq_list:=$(call shell_echo,$(CPP) $(CPPFLAGS) -P -Ichip/$(CHIP) \
		-I$(BASEDIR) -I$(BDIR) $(if $(HAVE_PRIVATE),-I$(ec-private)) \
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
	-Ichip/$(CHIP) -I$(BASEDIR) -I$(BDIR) \
	$(if $(HAVE_PRIVATE), -I$(ec-private)) -imacros include/config.h -
_rw_off_str:=$(call shell_echo,$(call cmd_config_eval,CONFIG_RW_MEM_OFF))
_rw_off:=$(shell echo "$$(($(_rw_off_str)))")
_rw_size_str:=$(call shell_echo,$(call cmd_config_eval,CONFIG_RW_SIZE))
_rw_size:=$(shell echo "$$(($(_rw_size_str)))")
_program_memory_base_str:=\
$(call shell_echo,$(call cmd_config_eval,CONFIG_PROGRAM_MEMORY_BASE))
_program_memory_base=$(shell echo "$$(($(_program_memory_base_str)))")

$(eval BASEBOARD_$(UC_BASEBOARD)=y)
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

# Include all subdirs under feature-x.
feature-x-builds = $(wildcard ../feature-x/*/ec/build.mk)
include $(feature-x-builds)

# Get build configuration from sub-directories
# Note that this re-includes the board and chip makefiles

ifdef CTS_MODULE
include cts/build.mk
endif
include $(BASEDIR)/build.mk
ifneq ($(BASEDIR),$(BDIR))
include $(BDIR)/build.mk
endif
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
include fuzz/build.mk
include power/build.mk
-include $(ec-private)/build.mk
ifneq ($(PDIR),)
include $(PDIR)/build.mk
endif
ifneq ($(PBDIR),)
include $(PBDIR)/build.mk
endif
include test/build.mk
include util/build.mk
include util/lock/build.mk


ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
include third_party/boringssl/common/build.mk
include crypto/build.mk
endif

# Collect all includes.
includes-y+=$(addprefix ../feature-x,$(feature-x-incs-y))
includes-y+=$(addprefix ../,$(druid-incs-y))
includes+=$(includes-y)

# Collect all build object output directories.
# This is different than the dirs variable, which serves as include path
# and build output directory creation.
dirs-y+=$(call objs_from_dir_p,feature-x,feature-x-dirs,y)
dirs-y+=$(call objs_from_dir_p,third_party/druid,druid-dirs,y)
dirs-y+=$(call objs_from_dir_p,test/third_party/druid,druid-dirs,y)

# Wrapper for fetching all the sources relevant to this build
# target.
# First arg is "y" indicating sources for all segments,
#   or "rw" indicating sources for rw segment.
define get_sources =
# Get sources to build for this target
all-obj-$(1)+=$(call objs_from_dir_p,core/$(CORE),core,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,chip/$(CHIP),chip,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,$(BASEDIR),baseboard,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,$(BDIR),board,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,feature-x,feature-x,$(1))
ifneq ($(PDIR),)
all-obj-$(1)+=$(call objs_from_dir_p,$(PDIR),$(PDIR),$(1))
endif
ifneq ($(PBDIR),)
all-obj-$(1)+= \
	$(call objs_from_dir_p,$(subst ../,$(empty),$(PBDIR)),board-private,$(1))
endif
all-obj-$(1)+=$(call objs_from_dir_p,common,common,$(1))
ifeq ($(USE_BUILTIN_STDLIB), 1)
all-obj-$(1)+=$(call objs_from_dir_p,builtin,builtin,$(1))
else
all-obj-$(1)+=$(call objs_from_dir_p,libc,libc,$(1))
endif
all-obj-$(1)+=$(call objs_from_dir_p,driver,driver,$(1))
all-obj-$(1)+=$(call objs_from_dir_p,power,power,$(1))
ifdef CTS_MODULE
all-obj-$(1)+=$(call objs_from_dir_p,cts,cts,$(1))
endif
ifeq ($(TEST_FUZZ),y)
all-obj-$(1)+=$(call objs_from_dir_p,fuzz,$(PROJECT),$(1))
else
all-obj-$(1)+=$(call objs_from_dir_p,test,$(PROJECT),$(1))
endif
ifeq ($(CONFIG_BORINGSSL_CRYPTO), y)
all-obj-$(1)+= \
    $(call objs_from_dir_p,third_party/boringssl/common,boringssl,$(1))
all-obj-$(1)+= $(call objs_from_dir_p,crypto,crypto,$(1))
endif
all-obj-$(1)+= $(call objs_from_dir_p,third_party/druid,druid,$(1))
endef

# Get all sources to build
$(eval $(call get_sources,y))
$(eval $(call get_sources,ro))

# The following variables are meant to be initialized in the baseboard or
# board's build.mk. They will later be appended to in util/build.mk with
# utils that should be generated for all boards.
#
# build-util-bin-y - Utils for the system doing the "build".
#                    For example, the 64-bit x86 GNU/Linux running make.
#                    These are often program that are needed by the build
#                    system to generate code for use in firmware.
# host-util-bin-y  - Utils for the target platform on top of the EC.
#                    For example, the 32-bit x86 Chromebook.
# build-util-art-y - Build ?artifacts? for the system doing the "build"
#
# The util targets added to these variable will pickup extra build objects
# from their optional <util_name>-objs make variable.
#
# See commit bc4c1b4 for more context.
build-utils := $(call objs_from_dir,$(out)/util,build-util-bin)
ifeq ($(BOARD),host)
host-utils := $(call objs_from_dir,$(out)/util,host-util-bin)
host-utils-cxx := $(call objs_from_dir,$(out)/util,host-util-bin-cxx)
endif
build-art := $(call objs_from_dir,$(out),build-util-art)
# Use the util_name with an added .c AND the special <util_name>-objs variable.
build-srcs := $(foreach u,$(build-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
                $(wildcard util/$(u).c)))
host-srcs := $(foreach u,$(host-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
               $(wildcard util/$(u).c)))
host-srcs-cxx := $(foreach u,$(host-util-bin-cxx-y), \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc)))

dirs=core/$(CORE) chip/$(CHIP) $(BASEDIR) $(BDIR) common fuzz power test \
	cts/common cts/$(CTS_MODULE) $(out)/gen
dirs+= $(ec-private) $(PDIR) $(PBDIR)
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

def_all_deps:=$(config) $(PROJECT_EXTRA) notice rw size utils
ifeq ($(CONFIG_FW_INCLUDE_RO),y)
def_all_deps+=ro
endif
all_deps?=$(def_all_deps)
all: $(all_deps)
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
