# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# MAX32660 chip specific files build
#

# MAX32660 SoC has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=clock_chip.o gpio_chip.o system_chip.o hwtimer_chip.o uart_chip.o
chip-$(CONFIG_I2C)+=i2c_chip.o

# Optional chip modules
chip-$(CONFIG_FLASH_PHYSICAL)+=flash_chip.o
chip-$(CONFIG_WATCHDOG)+=wdt_chip.o

