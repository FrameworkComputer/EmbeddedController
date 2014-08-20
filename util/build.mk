# -*- makefile -*-
# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay burn_my_ec stm32mon

comm-objs=$(util-lock-objs:%=lock/%) comm-host.o comm-dev.o
ifeq ($(CHIP),mec1322)
comm-objs+=comm-mec1322.o
else ifeq ($(CONFIG_LPC),y)
comm-objs+=comm-lpc.o
else
comm-objs+=comm-i2c.o
endif
ectool-objs=ectool.o ectool_keyscan.o misc_util.o ec_flash.o $(comm-objs) ../common/sha1.o
lbplay-objs=lbplay.o $(comm-objs)
burn_my_ec-objs=ec_flash.o $(comm-objs) misc_util.o

build-util-bin=ec_uartd iteflash
