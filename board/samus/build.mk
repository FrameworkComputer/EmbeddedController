# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is TI Stellaris LM4
CHIP:=lm4

# Samus has board-specific chipset code, and the tests don't
# compile with it. Disable them for now.
test-list-y=

board-y=battery.o board.o power_sequence.o panel.o extpower.o
