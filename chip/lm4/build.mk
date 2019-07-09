# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LM4 chip specific files build
#

# LM4 SoC has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o system.o uart.o

# Optional chip modules
chip-$(CONFIG_ADC)+=adc.o chip_temp_sensor.o
chip-$(CONFIG_EEPROM)+=eeprom.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_HOSTCMD_LPC)+=lpc.o
chip-$(CONFIG_PECI)+=peci.o
# pwm functions are implemented with the fan functions
chip-$(CONFIG_PWM)+=pwm.o fan.o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif
