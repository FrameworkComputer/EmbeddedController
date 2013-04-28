# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

test-list=pingpong timer_calib timer_dos timer_jump mutex thermal
test-list+=power_button kb_scan scancode typematic charging flash
test-list+=stress utils
#disable: powerdemo

flash-y=flash.o
kb_scan-y=kb_scan.o
mutex-y=mutex.o
pingpong-y=pingpong.o
powerdemo-y=powerdemo.o
stress-y=stress.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
utils-y=utils.o
