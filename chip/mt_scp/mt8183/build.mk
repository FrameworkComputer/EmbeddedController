# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y+=$(CHIP_VARIANT)/clock.o
chip-y+=$(CHIP_VARIANT)/gpio.o
chip-y+=$(CHIP_VARIANT)/memmap.o
chip-y+=$(CHIP_VARIANT)/system.o
chip-y+=$(CHIP_VARIANT)/uart.o

# Optional chip modules
chip-$(CONFIG_AUDIO_CODEC_WOV)+=$(CHIP_VARIANT)/audio_codec_wov.o
chip-$(CONFIG_COMMON_TIMER)+=$(CHIP_VARIANT)/hrtimer.o
chip-$(CONFIG_I2C)+=$(CHIP_VARIANT)/i2c.o
chip-$(CONFIG_IPI)+=$(CHIP_VARIANT)/ipi.o $(CHIP_VARIANT)/ipi_table.o
chip-$(CONFIG_SPI)+=$(CHIP_VARIANT)/spi.o
chip-$(CONFIG_WATCHDOG)+=$(CHIP_VARIANT)/watchdog.o

ifeq ($(CONFIG_IPI),y)
$(out)/RO/chip/$(CHIP)/$(CHIP_VARIANT)/ipi_table.o: $(out)/ipi_table_gen.inc
$(out)/RW/chip/$(CHIP)/$(CHIP_VARIANT)/ipi_table.o: $(out)/ipi_table_gen.inc
endif

ifeq ($(CONFIG_AUDIO_CODEC_WOV),y)
HOTWORD_PRIVATE_LIB:=private/libkukui_scp_google_hotword_dsp_api.a
ifneq ($(wildcard $(HOTWORD_PRIVATE_LIB)),)
LDFLAGS_EXTRA+=$(HOTWORD_PRIVATE_LIB)
HAVE_PRIVATE_AUDIO_CODEC_WOV_LIBS:=y
endif
endif
