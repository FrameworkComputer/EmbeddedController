# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

CORE:=host

chip-y=system.o gpio.o uart.o persistence.o flash.o lpc.o reboot.o \
	clock.o spi_master.o trng.o

ifndef CONFIG_KEYBOARD_NOT_RAW
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif
chip-$(CONFIG_USB_PD_TCPC)+=usb_pd_phy.o

ifeq ($(CONFIG_DCRYPTO),y)
CPPFLAGS += -I$(abspath ./chip/g)
dirs-y += chip/g/dcrypto
endif
dirs-y += chip/host/dcrypto

chip-$(CONFIG_I2C)+= i2c.o

chip-$(CONFIG_DCRYPTO)+= dcrypto/aes.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_cipher.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_key.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha256.o

# Object files that can be shared with the Cr50 dcrypto implementation
chip-$(CONFIG_DCRYPTO)+= ../g/dcrypto/hmac.o
