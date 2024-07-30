# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M0 core OS files build
#

# When set to 1, exclusively use builtins from compiler-rt.
# When set to 0, use EC's builtins.
USE_LLVM_COMPILER_RT:=0

ifeq ($(USE_LLVM_COMPILER_RT),1)
CFLAGS_CPU+=-DUSE_LLVM_COMPILER_RT
endif

# CPU specific compilation flags
CFLAGS_CPU+=-mthumb
ifeq ($(CROSS_COMPILE_CC_NAME),clang)
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
	"$(clang_resource_dir)/lib/baremetal/libclang_rt.builtins-armv6m.a"
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
ifeq ($(USE_LLVM_COMPILER_RT),0)
core-y+=div.o lmul.o ldivmod.o uldivmod.o
endif

core-y+=vecttable.o
ifeq ($(USE_LLVM_COMPILER_RT),0)
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

core-$(CONFIG_COMMON_PANIC_OUTPUT)+=exception_panic.o

$(CORE_RW_OUT)/exception_panic.o: $(CORE_RW_OUT)/asm_offsets.h
$(CORE_RW_OUT)/exception_panic.o: CFLAGS+=-I$(CORE_RW_OUT)

$(CORE_RO_OUT)/exception_panic.o: $(CORE_RO_OUT)/asm_offsets.h
$(CORE_RO_OUT)/exception_panic.o: CFLAGS+=-I$(CORE_RO_OUT)
