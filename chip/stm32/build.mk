# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# STM32 chip specific files build
#

ifeq ($(CHIP_FAMILY),stm32f0)
# STM32F0xx sub-family has a Cortex-M0 ARM core
CORE:=cortex-m0
# Force ARMv6-M ISA used by the Cortex-M0
CFLAGS_CPU+=-march=armv6-m -mcpu=cortex-m0
else
# other STM32 SoCs have a Cortex-M3 ARM core
CORE:=cortex-m
# Force Cortex-M3 subset of instructions
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3
endif

# STM32F0xx and STM32F1xx are using the same flash controller
FLASH_FAMILY=$(subst stm32f0,stm32f,$(CHIP_FAMILY))
# STM32F0xx chips will re-use STM32L I2C code
I2C_FAMILY=$(subst stm32f0,stm32l,$(CHIP_FAMILY))

chip-y=dma.o hwtimer.o system.o uart.o
chip-y+=jtag-$(CHIP_FAMILY).o clock-$(CHIP_FAMILY).o gpio-$(CHIP_FAMILY).o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_I2C)+=i2c-$(I2C_FAMILY).o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(HAS_TASK_POWERLED)+=power_led.o
chip-$(CONFIG_FLASH)+=flash-$(FLASH_FAMILY).o
chip-$(CONFIG_ADC)+=adc-$(CHIP_FAMILY).o
chip-$(CONFIG_PWM)+=pwm.o
