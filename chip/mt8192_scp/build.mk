# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# SCP specific files build
#

CORE:=riscv-rv32i

# Required chip modules
chip-y+=clock.o
chip-y+=gpio.o
chip-y+=intc.o
chip-y+=memmap.o
chip-y+=system.o
chip-y+=uart.o

ifeq ($(CONFIG_IPI),y)
$(out)/RW/chip/$(CHIP)/ipi_table.o: $(out)/ipi_table_gen.inc
endif

# Optional chip modules
chip-$(CONFIG_COMMON_TIMER)+=hrtimer.o
chip-$(CONFIG_IPI)+=ipi.o ipi_table.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_HOSTCMD)+=hostcmd.o
