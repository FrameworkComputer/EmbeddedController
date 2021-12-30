# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

BASEBOARD:=guybrush

board-y=board.o
board-y+=board_fw_config.o led.o battery.o thermal.o
