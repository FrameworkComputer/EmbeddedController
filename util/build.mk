# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

# See Makefile for description.
host-util-bin-cxx-y += ectool
host-util-bin-cxx-y += ec_parse_panicinfo
host-util-bin-cxx-y += lbplay
host-util-bin-cxx-y += stm32mon
ifneq ($(findstring -fsanitize=,$(HOST_CXXFLAGS)),-fsanitize=)
host-util-bin-cxx-y += stm32mon_static
endif
host-util-bin-cxx-y += lbcc
host-util-bin-cxx-y += iteflash
host-util-bin-cxx-y += itecomdbgr
host-util-bin-cxx-y += cbi-util
host-util-bin-cxx-y += ec_coredump

host-util-bin-y += rtkupdate
build-util-art-y += util/export_taskinfo.so

build-util-bin-$(CHIP_NPCX) += ecst
host-util-bin-cxx-$(BOARD_NOCTURNE_FP) += ectool_servo

host-util-bin-cxx-y += uartupdatetool
uartupdatetool-objs=uut/main.o uut/cmd.o uut/opr.o uut/l_com_port.o \
	uut/lib_crc.o
$(out)/util/uartupdatetool: HOST_CFLAGS+=-Iutil/

# b/381916046: statically linked version of stm32mon.
stm32mon_static-objs = stm32mon.o
$(out)/util/stm32mon_static: HOST_LDFLAGS+=-static

# If the util/ directory in the private repo is symlinked into util/private,
# we want to build host-side tools from it, too.
ifneq ("$(wildcard util/private/build.mk)","")
include util/private/build.mk
endif
-include private/util_flags.mk

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
comm-objs+=comm-lpc.o comm-i2c.o misc_util.o comm-usb.o

iteflash-objs = iteflash.o usb_if.o
$(out)/util/iteflash: HOST_LDFLAGS+=$(LIBFTDIUSB_HOST_LDLIBS)
itecomdbgr-objs = itecomdbgr.o
rtkupdate-objs = rtkupdate.o
ectool-objs=ectool.o ectool_keyscan.o ec_flash.o $(comm-objs)
ectool-objs+=ectool_i2c.o
ectool-objs+=ectool_pdc_trace.o
ectool-objs+=ectool_pdc_pcap.o
ectool-objs+=../common/crc.o
ectool_servo-objs=$(ectool-objs) comm-servo-spi.o
lbplay-objs=lbplay.o $(comm-objs)
$(out)/util/lbplay: HOST_LDFLAGS+=$(LIBFTDIUSB_HOST_LDLIBS)

util/ectool.cc: $(out)/ec_version.h
$(out)/util/ectool: HOST_LDFLAGS+=$(LIBEC_HOST_LDLIBS)

ec_parse_panicinfo-objs=ec_parse_panicinfo.o
$(out)/util/ec_parse_panicinfo: HOST_LDFLAGS+=$(LIBEC_HOST_LDLIBS)

ec_coredump-objs=ec_coredump.o $(comm-objs)
$(out)/util/ec_coredump: HOST_LDFLAGS+=$(LIBEC_HOST_LDLIBS)

# USB type-C Vendor Information File generation
ifeq ($(CONFIG_USB_POWER_DELIVERY),y)

STANDALONE_FLAGS=-ffreestanding -fno-builtin -nostdinc \
			-Ibuiltin/ -D"__keep= " -DVIF_BUILD=$(EMPTY)
$(out)/util/%/usb_pd_policy.o: %/usb_pd_policy.c
	-@ mkdir -p $(@D)
$(out)/util/%/usb_pd_pdo.o: %/usb_pd_pdo.c
	-@ mkdir -p $(@D)
$(out)/common/usb_common.o: common/usb_common.c
	-@ mkdir -p $(@D)
$(out)/common/usb_pd_pdo.o: common/usb_pd_pdo.c
	-@ mkdir -p $(@D)
endif # CONFIG_USB_POWER_DELIVERY

ifneq ($(CONFIG_BOOTBLOCK),)
build-util-bin-y += gen_emmc_transfer_data

# Bootblock is only packed in RO image.
$(out)/util/gen_emmc_transfer_data: BUILD_LDFLAGS += -DSECTION_IS_RO=$(EMPTY)
endif # CONFIG_BOOTBLOCK

ifneq ($(CONFIG_IPI),)
build-util-bin-y += gen_ipi_table

$(out)/util/gen_ipi_table: board/$(BOARD)/board.h
$(out)/ipi_table_gen.inc: $(out)/util/gen_ipi_table
	$(call quiet,ipi_table,IPITBL )
endif

cbi-util-objs=../common/crc8.o ../common/cbi.o

$(out)/util/export_taskinfo.so: $(out)/util/export_taskinfo_ro.o \
			$(out)/util/export_taskinfo_rw.o
	$(call quiet,link_taskinfo,BUILDLD)

$(out)/util/export_taskinfo_ro.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RO)

$(out)/util/export_taskinfo_rw.o: util/export_taskinfo.c
	$(call quiet,c_to_taskinfo,BUILDCC,RW)

deps-y += $(out)/util/export_taskinfo_ro.o.d $(out)/util/export_taskinfo_rw.o.d
