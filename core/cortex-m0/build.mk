# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M0 core OS files build
#

# Use coreboot-sdk
$(call set-option,CROSS_COMPILE,\
	$(CROSS_COMPILE_arm),\
	/opt/coreboot-sdk/bin/arm-eabi-)

# CPU specific compilation flags
CFLAGS_CPU+=-mthumb -Os -mno-sched-prolog
CFLAGS_CPU+=-mno-unaligned-access

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o init.o thumb_case.o div.o lmul.o ldivmod.o mula.o uldivmod.o
core-y+=vecttable.o __builtin.o
core-$(CONFIG_COMMON_PANIC_OUTPUT)+=panic.o
core-$(CONFIG_COMMON_RUNTIME)+=switch.o task.o

dirs-y += core/$(CORE)/curve25519

core-$(CONFIG_CURVE25519)+=curve25519/mpy121666.o
core-$(CONFIG_CURVE25519)+=curve25519/reduce25519.o
core-$(CONFIG_CURVE25519)+=curve25519/mul.o
core-$(CONFIG_CURVE25519)+=curve25519/scalarmult.o
core-$(CONFIG_CURVE25519)+=curve25519/sqr.o

core-$(CONFIG_WATCHDOG)+=watchdog.o
