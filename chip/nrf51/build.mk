# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# nRF51822 chip specific files build
#

CORE:=cortex-m0
# Force ARMv6-M ISA used by the Cortex-M0
CFLAGS_CPU+=-march=armv6-m -mcpu=cortex-m0

chip-y+=gpio.o system.o uart.o
chip-y+=jtag.o watchdog.o

chip-$(CONFIG_COMMON_TIMER)+=hwtimer.o clock.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
