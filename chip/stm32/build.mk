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

# Select between 16-bit and 32-bit timer for clock source
TIMER_TYPE=$(if $(CONFIG_STM_HWTIMER32),32,)

chip-y=dma.o system.o
chip-y+=jtag-$(CHIP_FAMILY).o clock-$(CHIP_FAMILY).o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_SPI_MASTER_PORT)+=spi_master.o
chip-$(CONFIG_COMMON_GPIO)+=gpio.o gpio-$(CHIP_FAMILY).o
chip-$(CONFIG_COMMON_TIMER)+=hwtimer$(TIMER_TYPE).o
chip-$(CONFIG_I2C)+=i2c-$(CHIP_FAMILY).o i2c.o
chip-$(CONFIG_STREAM_USART)+=usart.o usart-$(CHIP_FAMILY).o
chip-$(CONFIG_STREAM_USB)+=usb-stream.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_CONSOLE)+=uart.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(HAS_TASK_POWERLED)+=power_led.o
chip-$(CONFIG_FLASH)+=flash-$(CHIP_FAMILY).o
chip-$(CHIP_FAMILY_STM32F)+=flash-f.o
chip-$(CHIP_FAMILY_STM32F0)+=flash-f.o
chip-$(CHIP_FAMILY_STM32F3)+=flash-f.o
chip-$(CONFIG_ADC)+=adc-$(CHIP_FAMILY).o
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_USB)+=usb.o usb_endpoints.o
chip-$(CONFIG_USB_CONSOLE)+=usb_console.o
chip-$(CONFIG_USB_GPIO)+=usb_gpio.o
chip-$(CONFIG_USB_HID)+=usb_hid.o
chip-$(CONFIG_USB_MS)+=usb_ms.o usb_ms_scsi.o
chip-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_phy.o
