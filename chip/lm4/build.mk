# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LM4 chip specific files build
#

# CPU specific compilation flags
CFLAGS_CPU=-mcpu=cortex-m4 -mthumb -Os -mno-sched-prolog

chip-objs=init.o panic.o switch.o task.o timer.o pwm.o i2c.o adc.o jtag.o
chip-objs+=clock.o gpio.o system.o lpc.o uart.o x86_power.o power_button.o
chip-objs+=flash.o watchdog.o eeprom.o keyboard_scan.o temp_sensor.o
