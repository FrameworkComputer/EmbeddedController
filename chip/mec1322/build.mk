# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# MEC1322 chip specific files build
#

# MEC1322 SoC has a Cortex-M4 ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o system.o uart.o jtag.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_LPC)+=lpc.o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(CONFIG_DMA)+=dma.o
chip-$(CONFIG_SPI)+=spi.o

# location of the scripts and keys used to pack the SPI flash image
SCRIPTDIR:=./chip/${CHIP}/util

# commands to package something
cmd_pack_package = ${SCRIPTDIR}/pack_ec.py -o $@ -i $^ \
	--payload_key ${SCRIPTDIR}/rsakey_sign_payload.pem \
	--header_key ${SCRIPTDIR}/rsakey_sign_header.pem \
	--spi_size ${CHIP_SPI_SIZE_MB}
