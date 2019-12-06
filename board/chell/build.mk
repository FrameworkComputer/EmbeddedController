# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is SMSC MEC1322 / external SPI is 512KB / external clock is crystal
CHIP:=mec1322
CHIP_SPI_SIZE_KB:=512

board-y=board.o led.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
