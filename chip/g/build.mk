# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

SIGNED_RO_IMAGE = 1

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3

# Extract the hardware version we are building against
ver_defs := GC___MAJOR_REV__ GC___MINOR_REV__
bld_defs := GC_SWDP_BUILD_DATE_DEFAULT GC_SWDP_BUILD_TIME_DEFAULT
ver_params := $(shell echo "$(ver_defs) $(bld_defs)" | $(CPP) $(CPPFLAGS) -P \
                -imacros chip/g/${CHIP_VARIANT}_regdefs.h | sed -e "s/__REV\([A-Z]\)__/\1/")
ver_str := $(shell printf "%s%s %d_%d" $(ver_params))
CPPFLAGS+= -DGC_REVISION="$(ver_str)"

# Required chip modules
chip-y=clock.o gpio.o hwtimer.o jtag.o system.o
ifeq ($(CONFIG_POLLING_UART),y)
chip-y += polling_uart.o
else
chip-y += uart.o
endif
chip-y+= pmu.o
chip-y+= trng.o
chip-$(CONFIG_SPS)+= sps.o
chip-$(CONFIG_HOSTCMD_SPS)+=sps_hc.o
chip-$(CONFIG_TPM_SPS)+=sps_tpm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o

chip-$(CONFIG_USB)+=usb.o usb_endpoints.o
chip-$(CONFIG_USB_CONSOLE)+=usb_console.o
chip-$(CONFIG_USB_HID)+=usb_hid.o
chip-$(CONFIG_USB_BLOB)+=usb_blob.o

chip-$(CONFIG_FLASH)+=flash.o

custom-ro_objs-y  = chip/g/loader/main.o
custom-ro_objs-y += chip/g/system.o chip/g/uart.o
custom-ro_objs-y += common/printf.o
custom-ro_objs-y += common/util.o
custom-ro_objs-y += core/cortex-m/init.o
custom-ro_objs-y += core/cortex-m/panic.o

dirs-y += chip/g/loader

$(out)/RO/ec.RO.flat: $(out)/util/signer

$(out)/RO/ec.RO.hex: $(out)/RO/ec.RO.flat
