# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay stm32mon ec_sb_firmware_update lbcc
build-util-bin=ec_uartd iteflash

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
ifeq ($(CHIP),mec1322)
comm-objs+=comm-mec1322.o
else
comm-objs+=comm-lpc.o
endif
comm-objs+=comm-i2c.o

ectool-objs=ectool.o ectool_keyscan.o misc_util.o ec_flash.o $(comm-objs)
ec_sb_firmware_update-objs=ec_sb_firmware_update.o $(comm-objs) misc_util.o
lbplay-objs=lbplay.o $(comm-objs)
