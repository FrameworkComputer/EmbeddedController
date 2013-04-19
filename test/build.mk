# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

test-list=pingpong timer_calib timer_dos timer_jump mutex thermal
test-list+=power_button kb_scan scancode typematic charging flash
test-list+=stress
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

# Mock modules for 'scancode'
chip-mock-scancode-keyboard_scan_stub.o=mock_keyboard_scan_stub.o
common-mock-scancode-i8042.o=mock_i8042.o

# Mock modules for 'typematic'
chip-mock-typematic-keyboard_scan_stub.o=mock_keyboard_scan_stub.o
common-mock-typematic-i8042.o=mock_i8042.o

# Mock modules for 'charging'
chip-mock-charging-gpio.o=mock_gpio.o
common-mock-charging-x86_power.o=mock_x86_power.o
common-mock-charging-smart_battery_stub.o=mock_smart_battery_stub.o
common-mock-charging-charger_bq24725.o=mock_charger.o
