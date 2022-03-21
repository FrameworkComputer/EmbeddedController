# -*- makefile -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# SCP specific files build
#

CORE:=riscv-rv32i

# Required chip modules
chip-y+=rv32i_common/cache.o
chip-y+=rv32i_common/gpio.o
chip-y+=rv32i_common/intc.o
chip-y+=rv32i_common/memmap.o
chip-y+=rv32i_common/system.o
chip-y+=rv32i_common/uart.o

ifeq ($(CONFIG_IPI),y)
$(out)/RW/chip/$(CHIP)/rv32i_common/ipi_table.o: $(out)/ipi_table_gen.inc
endif

# Optional chip modules
chip-$(CONFIG_COMMON_TIMER)+=rv32i_common/hrtimer.o
chip-$(CONFIG_IPI)+=rv32i_common/ipi.o rv32i_common/ipi_table.o rv32i_common/ipi_ops.o
chip-$(CONFIG_WATCHDOG)+=rv32i_common/watchdog.o
chip-$(HAS_TASK_HOSTCMD)+=rv32i_common/hostcmd.o
