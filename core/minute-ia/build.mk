# -*- makefile -*-
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Minute-IA core build
#

# FPU compilation flags
CFLAGS_FPU-$(CONFIG_FPU)=

# CPU specific compilation flags
CFLAGS_CPU+=-O2 -fomit-frame-pointer					\
	    -ffunction-sections -fdata-sections				\
	    -fno-builtin-printf -fno-builtin-sprintf			\
	    -fno-stack-protector -gdwarf-2  -fno-common -ffreestanding	\
	    -minline-all-stringops -fno-strict-aliasing
ifneq ($(cc-name),clang)
CFLAGS_CPU+=-mno-accumulate-outgoing-args
endif

CFLAGS_CPU+=$(CFLAGS_FPU-y)

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o init.o interrupts.o
core-$(CONFIG_COMMON_PANIC_OUTPUT)+=panic.o
core-$(CONFIG_COMMON_RUNTIME)+=switch.o task.o
core-$(CONFIG_MPU)+=mpu.o

# for 64bit division
LDFLAGS_EXTRA+=-static-libgcc -lgcc
