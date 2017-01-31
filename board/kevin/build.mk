# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Nuvoton M-Series EC (npcx5m5g, npcx5m6g)
CHIP:=npcx
CHIP_VARIANT:=npcx5m5g

board-y=battery.o board.o charge_ramp.o usb_pd_policy.o
board-$(BOARD_GRU)+=led_gru.o
board-$(BOARD_KEVIN)+=led_kevin.o
