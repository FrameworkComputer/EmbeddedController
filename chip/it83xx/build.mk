# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# IT83xx chip specific files build
#

# IT8xxx1 and IT83xx are Andes N8 core.
# IT8xxx2 is RISC-V core.
ifeq ($(CHIP_FAMILY), it8xxx2)
CORE:=riscv-rv32i
else
CORE:=nds32
endif

# Required chip modules
chip-y=hwtimer.o uart.o gpio.o system.o clock.o irq.o intc.o

# Optional chip modules
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(CONFIG_FANS)+=fan.o pwm.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
# IT8xxx2 series use the FPU instruction set of RISC-V (single-precision only).
ifneq ($(CHIP_FAMILY), it8xxx2)
chip-$(CONFIG_FPU)+=it83xx_fpu.o
endif
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_DAC)+=dac.o
chip-$(CONFIG_HOSTCMD_X86)+=lpc.o ec2i.o
chip-$(CONFIG_HOSTCMD_ESPI)+=espi.o
chip-$(CONFIG_SPI_MASTER)+=spi_master.o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_PECI)+=peci.o
ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif
chip-$(CONFIG_I2C_MASTER)+=i2c.o
chip-$(CONFIG_I2C_SLAVE)+=i2c_slave.o
