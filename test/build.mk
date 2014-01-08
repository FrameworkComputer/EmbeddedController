# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

test-list-y=pingpong timer_calib timer_dos timer_jump mutex utils
#disable: powerdemo

test-list-$(BOARD_BDS)+=
test-list-$(BOARD_PIT)+=kb_scan stress
test-list-$(BOARD_SNOW)+=kb_scan stress
test-list-$(BOARD_SPRING)+=kb_scan stress

# Samus has board-specific chipset code, and the tests don't
# compile with it. Disable them for now.
test-list-$(BOARD_SAMUS)=

# Emulator tests
test-list-host=mutex pingpong utils kb_scan kb_mkbp lid_sw power_button hooks
test-list-host+=thermal flash queue kb_8042 extpwr_gpio console_edit system
test-list-host+=sbs_charging adapter host_command thermal_falco led_spring
test-list-host+=bklight_lid bklight_passthru interrupt timer_dos

adapter-y=adapter.o
bklight_lid-y=bklight_lid.o
bklight_passthru-y=bklight_passthru.o
console_edit-y=console_edit.o
extpwr_gpio-y=extpwr_gpio.o
flash-y=flash.o
hooks-y=hooks.o
host_command-y=host_command.o
kb_8042-y=kb_8042.o
interrupt-y=interrupt.o
kb_mkbp-y=kb_mkbp.o
kb_scan-y=kb_scan.o
led_spring-y=led_spring.o led_spring_impl.o
lid_sw-y=lid_sw.o
mutex-y=mutex.o
pingpong-y=pingpong.o
power_button-y=power_button.o
powerdemo-y=powerdemo.o
queue-y=queue.o
sbs_charging-y=sbs_charging.o
stress-y=stress.o
system-y=system.o
thermal-y=thermal.o
thermal_falco-y=thermal_falco.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
utils-y=utils.o
