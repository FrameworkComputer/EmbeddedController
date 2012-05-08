# -*- makefile -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LM4 chip specific files build
#

# LM4 SoC has a Cortex-M4 ARM core
CORE:=cortex-m

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o jtag.o system.o uart.o watchdog.o

# Optional chip modules
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_EEPROM)+=eeprom.o
chip-$(CONFIG_FLASH)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_LPC)+=lpc.o
chip-$(CONFIG_ONEWIRE)+=onewire.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_TASK_KEYSCAN)+=keyboard_scan.o
chip-$(CONFIG_TASK_POWERBTN)+=power_button.o
chip-$(CONFIG_TASK_TEMPSENSOR)+=chip_temp_sensor.o
