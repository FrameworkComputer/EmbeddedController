# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# MEC1322 chip specific files build
#

# MEC1322 SoC has a Cortex-M4 ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

ifeq ($(CONFIG_LTO),y)
# Re-include the core's build.mk file so we can remove the lto flag.
include core/$(CORE)/build.mk
endif

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o system.o uart.o port80.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_HOSTCMD_LPC)+=lpc.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif
chip-$(CONFIG_DMA)+=dma.o
chip-$(CONFIG_SPI)+=spi.o

# location of the scripts and keys used to pack the SPI flash image
SCRIPTDIR:=./chip/${CHIP}/util

# Allow SPI size to be overridden by board specific size, default to 256KB.
CHIP_SPI_SIZE_KB?=256

# Commands to convert $^ to $@.tmp
cmd_obj_to_bin = $(OBJCOPY) --gap-fill=0xff -O binary $< $@.tmp1 ; \
		 ${SCRIPTDIR}/pack_ec.py -o $@.tmp -i $@.tmp1 \
		--loader_file $(mec1322-lfw-flat) \
		--payload_key ${SCRIPTDIR}/rsakey_sign_payload.pem \
		--header_key ${SCRIPTDIR}/rsakey_sign_header.pem \
		--spi_size ${CHIP_SPI_SIZE_KB} \
		--image_size $(_rw_size); rm -f $@.tmp1

mec1322-lfw = chip/mec1322/lfw/ec_lfw
mec1322-lfw-flat = $(out)/RW/$(mec1322-lfw)-lfw.flat

# build these specifically for lfw with -lfw suffix
objs_lfw = $(patsubst %, $(out)/RW/%-lfw.o, \
		$(addprefix common/, util gpio) \
		$(addprefix chip/$(CHIP)/, spi dma gpio clock hwtimer) \
		core/$(CORE)/cpu $(mec1322-lfw))

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
$(out)/$(PROJECT).bin: $(mec1322-lfw-flat)
