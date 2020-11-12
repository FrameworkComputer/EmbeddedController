# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Microchip(MCHP) MEC chip specific files build
#

# pass verbose build setting to SPI image generation script
SCRIPTVERBOSE=
ifeq ($(V),1)
SCRIPTVERBOSE=--verbose
endif

# MCHP MEC SoC's have a Cortex-M4 ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# JTAG debug with Keil ARM MDK debugger
# do not allow GCC dwarf debug extensions
#CFLAGS_DEBUG_EXTRA=-gdwarf-3 -gstrict-dwarf

LDFLAGS_EXTRA=

ifeq ($(CONFIG_LTO),y)
# Re-include the core's build.mk file so we can remove the lto flag.
include core/$(CORE)/build.mk
endif

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o system.o uart.o port80.o tfdp.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_DMA)+=dma.o
chip-$(CONFIG_HOSTCMD_ESPI)+=espi.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_MEC_GPIO_EC_CMDS)+=gpio_cmds.o
chip-$(CONFIG_HOSTCMD_X86)+=lpc.o
chip-$(CONFIG_MCHP_GPSPI)+=gpspi.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_SPI)+=spi.o qmspi.o
chip-$(CONFIG_TFDP)+=tfdp.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif

# location of the scripts and keys used to pack the SPI flash image
SCRIPTDIR:=./chip/${CHIP}/util

# Allow SPI size to be overridden by board specific size, default to 512KB
CHIP_SPI_SIZE_KB?=512

TEST_SPI=
ifeq ($(CONFIG_MCHP_LFW_DEBUG),y)
	TEST_SPI=--test_spi
endif

# pack_ec.py creates SPI flash image for MEC
# _rw_size is CONFIG_RW_SIZE
# Commands to convert $^ to $@.tmp
cmd_obj_to_bin = $(OBJCOPY) --gap-fill=0xff -O binary $< $@.tmp1 ; \
		 ${SCRIPTDIR}/pack_ec.py -o $@.tmp -i $@.tmp1 \
		--loader_file $(chip-lfw-flat) ${TEST_SPI} \
		--spi_size ${CHIP_SPI_SIZE_KB} \
		--image_size $(_rw_size) --family $(UC_CHIP_FAMILY) ${SCRIPTVERBOSE}; rm -f $@.tmp1

chip-lfw = chip/${CHIP}/lfw/ec_lfw
chip-lfw-flat = $(out)/RW/$(chip-lfw)-lfw.flat

# build these specifically for lfw with -lfw suffix
objs_lfw = $(patsubst %, $(out)/RW/%-lfw.o, \
		$(addprefix common/, util gpio) \
		$(addprefix chip/$(CHIP)/, spi qmspi dma gpio clock hwtimer tfdp) \
		core/$(CORE)/cpu $(chip-lfw))

# reuse version.o (and its dependencies) from main board
objs_lfw += $(out)/RW/common/version.o

dirs-y+=chip/$(CHIP)/lfw

# objs with -lfw suffix are to include lfw's gpio
$(out)/RW/%-lfw.o: private CC+=-I$(BDIR)/lfw -DLFW=$(EMPTY)
# Remove the lto flag for the loader.  It actually causes it to bloat in size.
ifeq ($(CONFIG_LTO),y)
$(out)/RW/%-lfw.o: private CFLAGS_CPU := $(filter-out -flto, $(CFLAGS_CPU))
endif
$(out)/RW/%-lfw.o: %.c
	$(call quiet,c_to_o,CC     )

# let lfw's elf link only with selected objects
$(out)/RW/%-lfw.elf: private objs = $(objs_lfw)
$(out)/RW/%-lfw.elf: override shlib :=
$(out)/RW/%-lfw.elf: %.ld $(objs_lfw)
	$(call quiet,elf,LD     )

# final image needs lfw loader
$(out)/$(PROJECT).bin: $(chip-lfw-flat)
