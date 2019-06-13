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

ifeq ($(CONFIG_ISH_PM_AONTASK),y)

ish-aontask-fw=chip/ish/aontaskfw/ish_aontask
ish-aontask-dma=chip/ish/dma
ish-aontask-fw-bin=$(out)/$(ish-aontask-fw).bin
PROJECT_EXTRA+=$(ish-aontask-fw-bin)

_aon_size_str=$(shell stat -L -c %s $(ish-aontask-fw-bin))
_aon_size=$(shell echo "$$(($(_aon_size_str)))")

$(out)/$(PROJECT).bin: $(ish-aontask-fw-bin) $(out)/RW/$(PROJECT).RW.flat

endif

_kernel_size_str=$(shell stat -L -c %s $(out)/RW/$(PROJECT).RW.flat)
_kernel_size=$(shell echo "$$(($(_kernel_size_str)))")

# location of the scripts used to pack image
SCRIPTDIR:=./chip/${CHIP}/util


# Commands to convert ec.RW.flat to $@.tmp - This will add the manifest header
# needed to load the FW onto the ISH HW.

ifeq ($(CONFIG_ISH_PM_AONTASK),y)
cmd_obj_to_bin = ${SCRIPTDIR}/pack_ec.py -o $@.tmp \
		 -k $(out)/RW/$(PROJECT).RW.flat \
		 --kernel-size $(_kernel_size) \
		 -a $(ish-aontask-fw-bin)  \
		 --aon-size $(_aon_size);
else
cmd_obj_to_bin = ${SCRIPTDIR}/pack_ec.py -o $@.tmp \
		 -k $(out)/RW/$(PROJECT).RW.flat \
		 --kernel-size $(_kernel_size);
endif
