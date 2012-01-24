# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console.o vboot.o pwm_commands.o
common-y+=flash_commands.o port80.o
common-y+=memory_commands.o shared_mem.o temp_sensor_commands.o usb_charge.o
common-$(CONFIG_TASK_HOSTCMD)+=host_command.o
common-$(CONFIG_TASK_I8042CMD)+=i8042.o keyboard.o
common-$(CONFIG_TASK_X86POWER)+=x86_power.o
