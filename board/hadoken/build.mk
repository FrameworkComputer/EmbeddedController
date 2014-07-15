# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is Nordic nRF51822
CHIP:=nrf51
CHIP_FAMILY:=nrf51x22
CHIP_VARIANT:=nrf51822

board-y=board.o
