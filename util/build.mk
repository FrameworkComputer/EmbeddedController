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
