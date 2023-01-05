# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay stm32mon ec_sb_firmware_update lbcc \
	ec_parse_panicinfo cbi-util
build-util-bin=ec_uartd
build-util-art+=util/export_taskinfo.so
ifeq ($(CHIP),npcx)
build-util-bin+=ecst
endif
ifeq ($(CHIP_FAMILY),it8xxx2)
build-util-bin+=iteflash
endif
host-util-bin+=uartupdatetool
uartupdatetool-objs=uut/main.o uut/cmd.o uut/opr.o uut/l_com_port.o \
	uut/lib_crc.o
$(out)/util/uartupdatetool: HOST_CFLAGS+=-Iutil/
# Build on a limited subset of boards to save build time
ifeq ($(BOARD),nocturne_fp)
build-util-bin+=ectool_servo
endif

# If the util/ directory in the private repo is symlinked into util/private,
# we want to build host-side tools from it, too.
ifneq ("$(wildcard util/private/build.mk)","")
include util/private/build.mk
endif

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
comm-objs+=comm-lpc.o comm-mec_lpc.o comm-i2c.o misc_util.o

iteflash-objs = iteflash.o usb_if.o
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

# usb_pd_policy.c can be in baseboard, or board, or both.
genvif-pd-srcs=$(sort $(wildcard $(BASEDIR)/usb_pd_policy.c \
			board/$(BOARD)/usb_pd_policy.c))
genvif-pd-objs=$(genvif-pd-srcs:%.c=$(out)/util/%.o)
genvif-pd-objs += $(out)/common/usb_common.o
deps-$(CONFIG_USB_POWER_DELIVERY) += $(genvif-pd-objs:%.o=%.o.d)

$(out)/util/genvif: $(genvif-pd-objs) board/$(BOARD)/board.h \
			include/usb_pd.h include/usb_pd_tcpm.h
$(out)/util/genvif: BUILD_LDFLAGS+=$(genvif-pd-objs) -flto

STANDALONE_FLAGS=-ffreestanding -fno-builtin -nostdinc \
			-Ibuiltin/ -D"__keep= " -DVIF_BUILD=$(EMPTY)

$(out)/util/%/usb_pd_policy.o: %/usb_pd_policy.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
$(out)/common/usb_common.o: common/usb_common.c
	-@ mkdir -p $(@D)
	$(call quiet,c_to_vif,BUILDCC)
endif # CONFIG_USB_POWER_DELIVERY

ifneq ($(CONFIG_BOOTBLOCK),)
build-util-bin += gen_emmc_transfer_data

# Bootblock is only packed in RO image.
$(out)/util/gen_emmc_transfer_data: BUILD_LDFLAGS += -DSECTION_IS_RO=$(EMPTY)
endif # CONFIG_BOOTBLOCK

ifneq ($(CONFIG_IPI),)
build-util-bin += gen_ipi_table

$(out)/util/gen_ipi_table: board/$(BOARD)/board.h
$(out)/ipi_table_gen.inc: $(out)/util/gen_ipi_table
	$(call quiet,ipi_table,IPITBL )
endif

ifneq ($(CONFIG_TOUCHPAD_HASH_FW),)
build-util-bin += gen_touchpad_hash

# Assume RW section (touchpad FW must be identical for both RO+RW)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += -DSECTION_IS_RW=$(EMPTY)

OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --libs openssl)
OPENSSL_LDFLAGS := $(shell $(PKG_CONFIG) --libs openssl)

$(out)/util/gen_touchpad_hash: BUILD_CFLAGS += $(OPENSSL_CFLAGS)
$(out)/util/gen_touchpad_hash: BUILD_LDFLAGS += $(OPENSSL_LDFLAGS)

deps-y += $(out)/util/gen_touchpad_hash.d
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
