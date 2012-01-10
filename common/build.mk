# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-objs=main.o util.o console.o vboot.o x86_power.o pwm_commands.o
common-objs+=flash_commands.o host_command.o port80.o keyboard.o i8042.o
common-objs+=memory_commands.o shared_mem.o temp_sensor_commands.o usb_charge.o
