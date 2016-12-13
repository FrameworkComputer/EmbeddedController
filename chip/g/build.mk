# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

SIGNED_IMAGES = 1

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3

ifeq ($(CONFIG_DCRYPTO),y)
INCLUDE_ROOT := $(abspath ./include)
CRYPTOCLIB := $(realpath ../../third_party/cryptoc)
CPPFLAGS += -I$(abspath .)
CPPFLAGS += -I$(abspath ./builtin)
CPPFLAGS += -I$(abspath ./chip/$(CHIP))
CPPFLAGS += -I$(INCLUDE_ROOT)
CPPFLAGS += -I$(CRYPTOCLIB)/include
endif

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o jtag.o system.o
ifeq ($(CONFIG_POLLING_UART),y)
chip-y += polling_uart.o
else
chip-y += uart.o
chip-y += uartn.o
endif

chip-$(CONFIG_DCRYPTO)+= dcrypto/aes.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/bn.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/bn_hw.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/compare.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/dcrypto_runtime.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/hkdf.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/hmac.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ec.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ecies.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/rsa.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha1.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha256.o
ifeq ($(CONFIG_UPTO_SHA512),y)
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha384.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha512.o
endif
chip-$(CONFIG_DCRYPTO)+= dcrypto/x509.o

chip-$(CONFIG_SPI_MASTER)+=spi_master.o

chip-y+= jitter.o
chip-y+= pmu.o
chip-y+= trng.o
chip-y+= runlevel.o
chip-$(CONFIG_USB_FW_UPDATE)+= usb_upgrade.o
chip-$(CONFIG_NON_HC_FW_UPDATE)+= upgrade_fw.o
chip-$(CONFIG_SPS)+= sps.o
chip-$(CONFIG_TPM_SPS)+=sps_tpm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o

chip-$(CONFIG_USB)+=usb.o usb_endpoints.o
chip-$(CONFIG_USB_CONSOLE)+=usb_console.o
chip-$(CONFIG_USB_HID_KEYBOARD)+=usb_hid_keyboard.o
chip-$(CONFIG_USB_BLOB)+=blob.o
chip-$(CONFIG_USB_SPI)+=usb_spi.o
chip-$(CONFIG_RDD)+=rdd.o
chip-$(CONFIG_RBOX)+=rbox.o
chip-$(CONFIG_STREAM_USB)+=usb-stream.o
chip-$(CONFIG_STREAM_USART)+=usart.o
chip-$(CONFIG_I2C_MASTER)+= i2cm.o
chip-$(CONFIG_I2C_SLAVE)+= i2cs.o

chip-$(CONFIG_LOW_POWER_IDLE)+=idle.o

chip-$(CONFIG_FLASH_PHYSICAL) += flash.o
dirs-y += chip/g/dcrypto

ifneq ($(CONFIG_CUSTOMIZED_RO),)
custom-ro_objs-y  = chip/g/clock.o
custom-ro_objs-y += chip/g/dcrypto/sha256.o
custom-ro_objs-y += chip/g/loader/key_ladder.o
custom-ro_objs-y += chip/g/loader/debug_printf.o
custom-ro_objs-y += chip/g/loader/launch.o
custom-ro_objs-y += chip/g/loader/main.o
custom-ro_objs-y += chip/g/loader/rom_flash.o
custom-ro_objs-y += chip/g/loader/setup.o
custom-ro_objs-y += chip/g/loader/verify.o
custom-ro_objs-y += chip/g/pmu.o
custom-ro_objs-y += chip/g/system.o
custom-ro_objs-y += chip/g/trng.o
custom-ro_objs-y += chip/g/uart.o
custom-ro_objs-y += chip/g/uartn.o
custom-ro_objs-y += common/printf.o
custom-ro_objs-y += common/util.o
custom-ro_objs-y += core/cortex-m/init.o
custom-ro_objs-y += core/cortex-m/panic.o
dirs-y += chip/g/dcrypto
dirs-y += chip/g/loader
endif

$(out)/RO/ec.RO.flat: $(out)/util/signer
$(out)/RW/ec.RW.flat: $(out)/util/signer

%.hex: %.flat

ifneq ($(CONFIG_RW_B),)
$(out)/$(PROJECT).obj: $(out)/RW/ec.RW_B.flat
$(out)/RW/ec.RW_B.flat: $(out)/util/signer
endif

CR50_RO_KEY ?= rom-testkey-A.pem
ifeq ($(CR50_DEV),)
CR50_RW_KEY = loader-testkey-A.pem
SIGNER = $(out)/util/signer
SIGNER_EXTRAS =
else
CPPFLAGS += -DCR50_DEV=1
SIGNER = $(HOME)/bin/codesigner
CR50_RW_KEY = cr50_rom0-dev-blsign.pem.pub
RW_SIGNER_EXTRAS = -x util/signer/fuses.xml
RW_SIGNER_EXTRAS += -j util/signer/ec_RW-manifest-dev.json
$(out)/RW/ec.RW_B.flat: $(out)/RW/ec.RW.flat
$(out)/RW/ec.RW.flat $(out)/RW/ec.RW_B.flat: SIGNER_EXTRAS = $(RW_SIGNER_EXTRAS)
endif

# This file is included twice by the Makefile, once to determine the CHIP info
# # and then again after defining all the CONFIG_ and HAS_TASK variables. We use
# # a guard so that recipe definitions and variable extensions only happen the
# # second time.
ifeq ($(CHIP_MK_INCLUDED_ONCE),)
CHIP_MK_INCLUDED_ONCE=1
else

ifeq ($(CONFIG_DCRYPTO),y)
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += -L$(out)/cryptoc \
						-lcryptoc
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: $(out)/cryptoc/libcryptoc.a

# Force the external build each time, so it can look for changed sources.
.PHONY: $(out)/cryptoc/libcryptoc.a
$(out)/cryptoc/libcryptoc.a:
	$(MAKE) obj=$(realpath $(out))/cryptoc SUPPORT_UNALIGNED=1 \
		CONFIG_UPTO_SHA512=$(CONFIG_UPTO_SHA512) -C $(CRYPTOCLIB)
endif   # end CONFIG_DCRYPTO

endif   # CHIP_MK_INCLUDED_ONCE is nonempty
