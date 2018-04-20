# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay stm32mon ec_sb_firmware_update lbcc \
	ec_parse_panicinfo cbi-util
build-util-bin=ec_uartd iteflash
build-util-art+=util/export_taskinfo.so
ifeq ($(CHIP),npcx)
build-util-bin+=ecst
endif
host-util-bin+=uartupdatetool
uartupdatetool-objs=uut/main.o uut/cmd.o uut/opr.o uut/l_com_port.o \
	uut/lib_crc.o
$(out)/util/uartupdatetool: HOST_CFLAGS+=-Iutil/
# Build on a limited subset of boards to save build time
ifeq ($(BOARD),meowth_fp)
build-util-bin+=ectool_servo
endif

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
comm-objs+=comm-lpc.o comm-i2c.o misc_util.o

ectool-objs=ectool.o ectool_keyscan.o ec_flash.o ec_panicinfo.o $(comm-objs)
ectool_servo-objs=$(ectool-objs) comm-servo-spi.o
ec_sb_firmware_update-objs=ec_sb_firmware_update.o $(comm-objs) misc_util.o
ec_sb_firmware_update-objs+=powerd_lock.o
lbplay-objs=lbplay.o $(comm-objs)

util/ectool.c: $(out)/ec_version.h

ec_parse_panicinfo-objs=ec_parse_panicinfo.o ec_panicinfo.o

# USB type-C Vendor Information File generation
ifeq ($(CONFIG_USB_POWER_DELIVERY),y)
build-util-bin+=genvif
build-util-art+=$(BOARD)_vif.txt
$(out)/util/genvif: $(out)/util/usb_pd_policy.o board/$(BOARD)/board.h \
			include/usb_pd.h include/usb_pd_tcpm.h
$(out)/util/genvif: BUILD_LDFLAGS+=$(out)/util/usb_pd_policy.o -flto

STANDALONE_FLAGS=-ffreestanding -fno-builtin -nostdinc \
			-Ibuiltin/ -D"__keep= " -DVIF_BUILD

# If baseboard is defined, include its usb_pd_policy; otherwise,
# $(BASEDIR) will alias to `board/$(BOARD)` and includes same board file twice.
$(out)/util/usb_pd_policy.o: $(BASEDIR)/usb_pd_policy.c \
	board/$(BOARD)/usb_pd_policy.c
	$(call quiet,c_to_vif,BUILDCC)
deps-$(CONFIG_USB_POWER_DELIVERY) += $(out)/util/usb_pd_policy.o.d
endif # CONFIG_USB_POWER_DELIVERY

ifneq ($(CONFIG_TOUCHPAD_HASH_FW),)
build-util-bin += gen_touchpad_hash

# Assume RW section (touchpad FW must be identical for both RO+RW)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += -DSECTION_IS_RW

OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --libs openssl)
OPENSSL_LDFLAGS := $(shell $(PKG_CONFIG) --libs openssl)

$(out)/util/gen_touchpad_hash: BUILD_CFLAGS += $(OPENSSL_CFLAGS)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += $(OPENSSL_LDFLAGS)
endif # CONFIG_TOUCHPAD_VIRTUAL_OFF

cbi-util-objs=../common/crc8.o ../common/cbi.o

$(out)/util/export_taskinfo.so: $(out)/util/export_taskinfo_ro.o \
			$(out)/util/export_taskinfo_rw.o
	$(call quiet,link_taskinfo,BUILDLD)

$(out)/util/export_taskinfo_ro.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RO)

$(out)/util/export_taskinfo_rw.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RW)

deps-y += $(out)/util/export_taskinfo_ro.o.d $(out)/util/export_taskinfo_rw.o.d
