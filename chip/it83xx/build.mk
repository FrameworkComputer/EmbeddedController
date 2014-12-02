# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# IT83xx chip specific files build
#

# IT83xx SoC family has an Andes N801 core.
CORE:=nds32

# Required chip modules
chip-y=hwtimer.o uart.o gpio.o system.o jtag.o clock.o irq.o

# Optional chip modules
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_EC2I)+=ec2i.o
