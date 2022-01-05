# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y+=mt818x/clock_$(CHIP_VARIANT).o
chip-y+=mt818x/gpio.o
chip-y+=mt818x/memmap.o
chip-y+=mt818x/system.o
chip-y+=mt818x/uart.o

# Optional chip modules
chip-$(CONFIG_AUDIO_CODEC_WOV)+=mt818x/audio_codec_wov.o
chip-$(CONFIG_COMMON_TIMER)+=mt818x/hrtimer.o
chip-$(CONFIG_I2C)+=mt818x/i2c.o
chip-$(CONFIG_IPI)+=mt818x/ipi.o mt818x/ipi_table.o
chip-$(CONFIG_SPI)+=mt818x/spi.o
chip-$(CONFIG_WATCHDOG)+=mt818x/watchdog.o

ifeq ($(CONFIG_IPI),y)
$(out)/RO/chip/$(CHIP)/mt818x/ipi_table.o: $(out)/ipi_table_gen.inc
$(out)/RW/chip/$(CHIP)/mt818x/ipi_table.o: $(out)/ipi_table_gen.inc
endif

ifeq ($(CONFIG_AUDIO_CODEC_WOV),y)
HOTWORD_PRIVATE_LIB:=private/libkukui_scp_google_hotword_dsp_api.a
ifneq ($(wildcard $(HOTWORD_PRIVATE_LIB)),)
LDFLAGS_EXTRA+=$(HOTWORD_PRIVATE_LIB)
HAVE_PRIVATE_AUDIO_CODEC_WOV_LIBS:=y
endif
endif
