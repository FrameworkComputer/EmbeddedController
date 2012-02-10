# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LM4 chip specific files build
#

# LM4 SoC has a Cortex-M4 ARM core
CORE:=cortex-m

chip-y=i2c.o adc.o jtag.o
chip-y+=clock.o gpio.o system.o uart.o power_button.o
chip-y+=watchdog.o eeprom.o hwtimer.o
chip-$(CONFIG_FLASH)+=flash.o
chip-$(CONFIG_LPC)+=lpc.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_TEMP_SENSOR)+=chip_temp_sensor.o
chip-$(CONFIG_TASK_KEYSCAN)+=keyboard_scan.o
