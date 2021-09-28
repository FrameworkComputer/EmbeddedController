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
CFLAGS_CPU+=-mthumb
ifeq ($(cc-name),clang)
CFLAGS_CPU+=-Oz		# Like -Os (and thus -O2), but reduces code size further.
# Link compiler-rt when using clang, so clang finds the builtins it provides.
LDFLAGS_EXTRA+=-lclang_rt.builtins-arm
else
CFLAGS_CPU+=-Os
CFLAGS_CPU+=-mno-sched-prolog
endif
CFLAGS_CPU+=-mno-unaligned-access

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o debug.o init.o thumb_case.o mula.o
# When using clang, we get these as builtins from compiler-rt.
ifneq ($(cc-name),clang)
core-y+=div.o lmul.o ldivmod.o uldivmod.o
endif

core-y+=vecttable.o
# When using clang, we get these as builtins from compiler-rt.
ifneq ($(cc-name),clang)
core-y+=__builtin.o
endif
core-$(CONFIG_COMMON_PANIC_OUTPUT)+=panic.o
core-$(CONFIG_COMMON_RUNTIME)+=switch.o task.o

dirs-y += core/$(CORE)/curve25519

core-$(CONFIG_CURVE25519)+=curve25519/mpy121666.o
core-$(CONFIG_CURVE25519)+=curve25519/reduce25519.o
core-$(CONFIG_CURVE25519)+=curve25519/mul.o
core-$(CONFIG_CURVE25519)+=curve25519/scalarmult.o
core-$(CONFIG_CURVE25519)+=curve25519/sqr.o

core-$(CONFIG_WATCHDOG)+=watchdog.o
