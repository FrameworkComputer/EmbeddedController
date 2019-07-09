# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# nRF51822 chip specific files build
#

CORE:=cortex-m0
# Force ARMv6-M ISA used by the Cortex-M0
# For historical reasons gcc calls it armv6s-m: ARM used to have ARMv6-M
# without "svc" instruction, but that was short-lived. ARMv6S-M was the option
# with "svc". GCC kept that naming scheme even though the distinction is long
# gone.
CFLAGS_CPU+=-march=armv6s-m -mcpu=cortex-m0

chip-y+=gpio.o system.o uart.o
chip-y+=watchdog.o ppi.o

chip-$(CONFIG_BLUETOOTH_LE)+=radio.o bluetooth_le.o
chip-$(CONFIG_BLUETOOTH_LE_RADIO_TEST)+=radio_test.o
chip-$(CONFIG_COMMON_TIMER)+=hwtimer.o clock.o
chip-$(CONFIG_I2C)+=i2c.o
ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif
