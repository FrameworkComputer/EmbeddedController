# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# STM32L chip specific files build
#

# STM32L15xx SoC family has a Cortex-M3 ARM core
CORE:=cortex-m

chip-y=uart.o clock.o hwtimer.o system.o
chip-$(CONFIG_TASK_WATCHDOG)+=watchdog.o
