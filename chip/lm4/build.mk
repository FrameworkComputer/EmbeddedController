# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LM4 chip specific files build
#

# LM4 SoC has a Cortex-M4 ARM core
CORE:=cortex-m

chip-y=pwm.o i2c.o adc.o jtag.o
chip-y+=clock.o gpio.o system.o lpc.o uart.o power_button.o
chip-y+=flash.o watchdog.o eeprom.o temp_sensor.o hwtimer.o
chip-$(CONFIG_TASK_KEYSCAN)+=keyboard_scan.o
