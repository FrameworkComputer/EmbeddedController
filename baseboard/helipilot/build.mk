# -*- makefile -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helipilot baseboard specific files build
#

baseboard-y += base_board.o
baseboard-rw += base_board_rw.o
baseboard-y += fpsensor_detect.o
baseboard-rw += fpsensor_detect_rw.o
