# -*- makefile -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

test-list=hello pingpong timer_calib timer_dos timer_jump mutex thermal
test-list+=power_button
#disable: powerdemo

pingpong-y=pingpong.o
powerdemo-y=powerdemo.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
mutex-y=mutex.o

# Mock modules for 'thermal'
chip-mock-thermal-lpc.o=mock_lpc.o
chip-mock-thermal-pwm.o=mock_pwm.o
common-mock-thermal-x86_power.o=mock_x86_power.o
common-mock-thermal-temp_sensor.o=mock_temp_sensor.o

# Mock modules for 'power_button'
chip-mock-power_button-gpio.o=mock_gpio.o
chip-mock-power_button-pwm.o=mock_pwm.o
common-mock-power_button-x86_power.o=mock_x86_power.o
common-mock-power_button-i8042.o=mock_i8042.o
