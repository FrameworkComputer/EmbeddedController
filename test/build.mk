# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

test-list-y=pingpong timer_calib timer_dos timer_jump mutex utils
#disable: powerdemo

# TODO(victoryang): Fix these tests:
#    thermal power_button scancode typematic charging

test-list-$(BOARD_bds)+=
test-list-$(BOARD_daisy)+=kb_scan flash stress
test-list-$(BOARD_mccroskey)+=flash
test-list-$(BOARD_pit)+=kb_scan flash stress
test-list-$(BOARD_snow)+=kb_scan flash stress
test-list-$(BOARD_spring)+=kb_scan flash stress

# Disable x86 boards until they compiles
# TODO(victoryang): Fix them
test-list-$(BOARD_link)=
test-list-$(BOARD_slippy)=

flash-y=flash.o
kb_mkbp-y=kb_mkbp.o
kb_scan-y=kb_scan.o
mutex-y=mutex.o
pingpong-y=pingpong.o
powerdemo-y=powerdemo.o
stress-y=stress.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
utils-y=utils.o
