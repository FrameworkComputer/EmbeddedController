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
chip-y=hwtimer.o uart.o gpio.o system.o clock.o irq.o intc.o

# Optional chip modules
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(CONFIG_FANS)+=fan.o pwm.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
chip-$(CONFIG_FPU)+=it83xx_fpu.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_HOSTCMD_X86)+=lpc.o ec2i.o
chip-$(CONFIG_HOSTCMD_ESPI)+=espi.o
chip-$(CONFIG_SPI_MASTER)+=spi_master.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(CONFIG_I2C_MASTER)+=i2c.o
chip-$(CONFIG_I2C_SLAVE)+=i2c_slave.o
