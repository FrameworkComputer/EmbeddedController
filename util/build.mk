# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay stm32mon ec_sb_firmware_update lbcc \
	ec_parse_panicinfo
build-util-bin=ec_uartd iteflash
ifeq ($(CHIP),npcx)
build-util-bin+=ecst
endif

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
comm-objs+=comm-lpc.o comm-i2c.o misc_util.o

ectool-objs=ectool.o ectool_keyscan.o ec_flash.o ec_panicinfo.o $(comm-objs)
ec_sb_firmware_update-objs=ec_sb_firmware_update.o $(comm-objs) misc_util.o
ec_sb_firmware_update-objs+=powerd_lock.o
lbplay-objs=lbplay.o $(comm-objs)

ec_parse_panicinfo-objs=ec_parse_panicinfo.o ec_panicinfo.o

# USB type-C Vendor Information File generation
ifeq ($(CONFIG_USB_POWER_DELIVERY),y)
build-util-bin+=genvif
build-util-art+=$(BOARD)_vif.txt
$(out)/util/genvif: $(out)/util/usb_pd_policy.o board/$(BOARD)/board.h \
			include/usb_pd.h include/usb_pd_tcpm.h
$(out)/util/genvif: BUILD_LDFLAGS+=$(out)/util/usb_pd_policy.o -flto

STANDALONE_FLAGS=-ffreestanding -fno-builtin -nostdinc -Ibuiltin/ -D"__keep= "
$(out)/util/usb_pd_policy.o: board/$(BOARD)/usb_pd_policy.c
	$(call quiet,c_to_vif,BUILDCC)
deps += $(out)/util/usb_pd_policy.o.d
endif # CONFIG_USB_POWER_DELIVERY
