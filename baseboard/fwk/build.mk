# -*- makefile -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o diagnostics.o flash_storage.o
baseboard-$(CONFIG_BATTERY_SMART)+=battery.o
baseboard-$(CONFIG_FANS)+=fan.o
baseboard-$(CONFIG_SYSTEMSERIAL_DEBUG) += system_serial.o
baseboard-$(CONFIG_8042_AUX) += ps2mouse.o
baseboard-$(HAS_TASK_HOSTCMD) += baseboard_host_commands.o
