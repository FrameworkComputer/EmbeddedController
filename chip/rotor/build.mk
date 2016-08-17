# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Rotor chip specific files build
#

# Rotor has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o jtag.o system.o uart.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_SPI_MASTER)+=spi_master.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o

# Rotor MCU only supports one image, so only build RW.
all_deps=$(patsubst ro,,$(def_all_deps))
