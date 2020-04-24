# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# ISH chip specific files build
#

# ISH SoC has a Minute-IA core
CORE:=minute-ia
# Allow the i486 instruction set
CFLAGS_CPU+=-march=pentium -mtune=i486 -m32

ifeq ($(CONFIG_LTO),y)
# Re-include the core's build.mk file so we can remove the lto flag.
include core/$(CORE)/build.mk
endif

# Required chip modules
chip-y+=clock.o gpio.o system.o hwtimer.o uart.o flash.o ish_persistent_data.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(CONFIG_HOSTCMD_HECI)+=host_command_heci.o
chip-$(CONFIG_HOSTCMD_HECI)+=heci.o system_state_subsys.o ipc_heci.o
chip-$(CONFIG_HID_HECI)+=hid_subsys.o
chip-$(CONFIG_HID_HECI)+=heci.o system_state_subsys.o ipc_heci.o
chip-$(CONFIG_DMA_PAGING)+=dma.o
chip-$(CONFIG_LOW_POWER_IDLE)+=power_mgt.o

# There is no framework for on-board tests in ISH. Do not specify any.
test-list-y=

# Build ish aon task fw
ish-aon-name=ish_aontask
ish-aon-$(CONFIG_ISH_PM_AONTASK)=aontaskfw/ish_aontask.o dma.o
ish-aon-$(CONFIG_ISH_IPAPG)+=aontaskfw/ipapg.o

# Rules for building ish aon task fw
ish-aon-out=$(out)/aontaskfw
ish-aon-bin-$(CONFIG_ISH_PM_AONTASK)=$(ish-aon-out)/$(ish-aon-name).bin
ish-aon-elf-$(CONFIG_ISH_PM_AONTASK)=$(ish-aon-out)/$(ish-aon-name).elf
ish-aon-lds-$(CONFIG_ISH_PM_AONTASK)=$(ish-aon-out)/$(ish-aon-name).lds

ish-aon-objs=$(call objs_from_dir,$(ish-aon-out)/chip/$(CHIP),ish-aon)
ish-aon-deps+=$(addsuffix .d, $(ish-aon-objs)) $(ish-aon-lds-y).d

cmd_ish_aon_elf = $(CC) $(ish-aon-objs) $(LDFLAGS) \
	-o $@ -Wl,-T,$< -Wl,-Map,$(patsubst %.elf,%.map,$@)

PROJECT_EXTRA+=$(ish-aon-bin-y)
deps-$(CONFIG_ISH_PM_AONTASK)+=$(ish-aon-deps)

$(out)/$(PROJECT).bin: $(ish-aon-bin-y) $(out)/RW/$(PROJECT).RW.flat

$(ish-aon-bin-y): $(ish-aon-elf-y)
	$(call quiet,elf_to_bin,EXTBIN )

$(ish-aon-elf-y): $(ish-aon-lds-y) $(ish-aon-objs)
	$(call quiet,ish_aon_elf,LD     )

$(ish-aon-lds-y): chip/$(CHIP)/aontaskfw/ish_aontask.lds.S
	-@ mkdir -p $(@D)
	$(call quiet,lds,LDS    )

$(ish-aon-out)/%.o: %.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_o,CC     )

$(ish-aon-out)/%.o: %.S
	-@ mkdir -p $(@D)
	$(call quiet,c_to_o,AS     )

# Location of the scripts used to pack image
SCRIPTDIR:=./chip/${CHIP}/util

# Calculate aon binary file size and kernel binary file size
_aon_size_str=$(shell stat -L -c %s $(ish-aon-bin-y))
_aon_size=$(shell echo "$$(($(_aon_size_str)))")

_kernel_size_str=$(shell stat -L -c %s $(out)/RW/$(PROJECT).RW.flat)
_kernel_size=$(shell echo "$$(($(_kernel_size_str)))")

# Commands to convert ec.RW.flat to $@.tmp - This will add the manifest header
# needed to load the FW onto the ISH HW.

ifeq ($(CONFIG_ISH_PM_AONTASK),y)
cmd_obj_to_bin = ${SCRIPTDIR}/pack_ec.py -o $@.tmp \
		 -k $(out)/RW/$(PROJECT).RW.flat \
		 --kernel-size $(_kernel_size) \
		 -a $(ish-aon-bin-y)  \
		 --aon-size $(_aon_size);
else
cmd_obj_to_bin = ${SCRIPTDIR}/pack_ec.py -o $@.tmp \
		 -k $(out)/RW/$(PROJECT).RW.flat \
		 --kernel-size $(_kernel_size);
endif
