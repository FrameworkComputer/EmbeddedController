# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

CORE:=host

chip-y=system.o gpio.o uart.o persistence.o flash.o lpc.o reboot.o i2c.o \
	clock.o spi_master.o trng.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
chip-$(CONFIG_USB_PD_TCPC)+=usb_pd_phy.o

ifeq ($(CONFIG_DCRYPTO),y)
CPPFLAGS += -I$(abspath ./chip/g)
dirs-y += chip/g/dcrypto
endif
dirs-y += chip/host/dcrypto

chip-$(CONFIG_DCRYPTO)+= dcrypto/aes.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_cipher.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_key.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha256.o

# Object files that can be shared with the Cr50 dcrypto implementation
chip-$(CONFIG_DCRYPTO)+= ../g/dcrypto/hmac.o

ifeq ($(CONFIG_DCRYPTO),y)
CRYPTOCLIB := $(realpath ../../third_party/cryptoc)

# Force the external build each time, so it can look for changed sources.
.PHONY: $(out)/cryptoc/libcryptoc.a
$(out)/cryptoc/libcryptoc.a:
	$(MAKE) obj=$(realpath $(out))/cryptoc SUPPORT_UNALIGNED=1 \
		CONFIG_UPTO_SHA512=$(CONFIG_UPTO_SHA512) -C $(CRYPTOCLIB)

CPPFLAGS += -I$(CRYPTOCLIB)/include
endif   # end CONFIG_DCRYPTO
