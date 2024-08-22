# -*- makefile -*-
# Copyright 2012 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M4 core OS files build
#

# FPU compilation flags
CFLAGS_FPU-$(CONFIG_FPU)=-mfloat-abi=hard
ifeq ($(cc-name),gcc)
# -mfpu=auto will choose correct hardware based on settings of -mcpu and -march
# https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html.
#
# According to the above doc "-mfpu=auto" is the default and shouldn't be
# required, but compilation using gcc fails without the flag.
#
# clang does not support "-mfpu=auto" flag, but will choose the correct floating
# point unit based on the -mcpu flag:
# https://lists.llvm.org/pipermail/llvm-dev/2018-September/126468.html
CFLAGS_FPU-$(CONFIG_FPU)+=-mfpu=auto
endif

# CPU specific compilation flags
CFLAGS_CPU+=-mthumb
ifeq ($(cc-name),clang)
CFLAGS_CPU+=-Oz		# Like -Os (and thus -O2), but reduces code size further.
# b/256193799: Reduce inline threshold to decrease code size.
CFLAGS_CPU+=-Wl,-mllvm -Wl,-inline-threshold=-10
# Explicitly specify libclang_rt.builtins so that its symbols are preferred
# over libc's. This avoids duplicate symbol errors. See b/346309204 for details.
clang_resource_dir:="$(shell $(CC) --print-resource-dir)"
ifneq ($(.SHELLSTATUS),0)
$(error Could not determine path to libclang_rt.builtins)
endif
LDFLAGS_EXTRA+=\
	"$(clang_resource_dir)/lib/baremetal/libclang_rt.builtins-armv7m.a"
else
CFLAGS_CPU+=-Os
CFLAGS_CPU+=-mno-sched-prolog
endif
CFLAGS_CPU+=-mno-unaligned-access
CFLAGS_CPU+=$(CFLAGS_FPU-y)

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

core-y=cpu.o debug.o init.o vecttable.o
# When using clang, we get these as builtins from compiler-rt.
ifneq ($(cc-name),clang)
core-y+=ldivmod.o llsr.o uldivmod.o
endif
core-$(CONFIG_ARMV7M_CACHE)+=cache.o
core-$(CONFIG_COMMON_PANIC_OUTPUT)+=panic.o
core-$(CONFIG_COMMON_RUNTIME)+=switch.o task.o
core-$(CONFIG_FPU)+=fpu.o
core-$(CONFIG_WATCHDOG)+=watchdog.o
core-$(CONFIG_MPU)+=mpu.o
