# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# STM32 chip specific files build
#

# STM32 SoC family has a Cortex-M3 ARM core
CORE:=cortex-m

chip-y=dma.o hwtimer.o system.o uart.o
chip-y+=jtag-$(CHIP_VARIANT).o clock-$(CHIP_VARIANT).o gpio-$(CHIP_VARIANT).o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(CONFIG_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(CONFIG_TASK_POWERLED)+=power_led.o
chip-$(CONFIG_FLASH)+=flash-$(CHIP_VARIANT).o
chip-$(CONFIG_ADC)+=adc.o
