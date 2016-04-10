# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

SIGNED_IMAGES = 1

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3

# Extract the hardware version we are building against
ver_defs := GC___MAJOR_REV__ GC___MINOR_REV__
bld_defs := GC_SWDP_BUILD_DATE_DEFAULT GC_SWDP_BUILD_TIME_DEFAULT
ver_params := $(shell echo "$(ver_defs) $(bld_defs)" | $(CPP) $(CPPFLAGS) -P \
                -imacros chip/g/hw_regdefs.h | sed -e "s/__REV\([A-Z]\)__/\1/")
ver_str := $(shell printf "%s%s %d_%d" $(ver_params))
CPPFLAGS+= -DGC_REVISION="$(ver_str)"

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
chip-$(CONFIG_DCRYPTO)+= dcrypto/hmac.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ec.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ecdsa.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/rsa.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha1.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha256.o

chip-$(CONFIG_SPI_MASTER)+=spi_master.o

chip-y+= pmu.o
chip-y+= trng.o
chip-$(CONFIG_NON_HC_FW_UPDATE)+= upgrade_fw.o
chip-$(CONFIG_SPS)+= sps.o
chip-$(CONFIG_TPM_SPS)+=sps_tpm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o

chip-$(CONFIG_USB)+=usb.o usb_endpoints.o
chip-$(CONFIG_USB_CONSOLE)+=usb_console.o
chip-$(CONFIG_USB_HID)+=usb_hid.o
chip-$(CONFIG_USB_BLOB)+=blob.o
chip-$(CONFIG_STREAM_USB)+=usb-stream.o
chip-$(CONFIG_STREAM_USART)+=usart.o

chip-$(CONFIG_LOW_POWER_IDLE)+=idle.o

chip-$(CONFIG_FLASH)+=flash.o
dirs-y += chip/g/dcrypto

ifneq ($(CONFIG_CUSTOMIZED_RO),)
custom-ro_objs-y  = chip/g/clock.o
custom-ro_objs-y += chip/g/dcrypto/sha256.o
custom-ro_objs-y += chip/g/loader/key_ladder.o
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
