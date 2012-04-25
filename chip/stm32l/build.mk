# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# STM32L chip specific files build
#

# STM32L15xx SoC family has a Cortex-M3 ARM core
CORE:=cortex-m

chip-y=clock.o dma.o gpio.o hwtimer.o jtag.o system.o uart.o
chip-$(CONFIG_TASK_SPI_WORK)+=spi.o
chip-$(CONFIG_TASK_I2C2_WORK)+=i2c.o
chip-$(CONFIG_TASK_WATCHDOG)+=watchdog.o
chip-$(CONFIG_TASK_KEYSCAN)+=keyboard_scan.o
