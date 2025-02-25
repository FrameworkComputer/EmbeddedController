# -*- makefile -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# RISC-V core OS files build
#

# Enable FPU extension if config option of FPU is enabled.
_FPU_EXTENSION=$(if $(CONFIG_FPU),f,)
# Enable the 'M' extension if config option of RISCV_EXTENSION_M is enabled.
_M_EXTENSION=$(if $(CONFIG_RISCV_EXTENSION_M),m,)
# Enable the zifencei extension
_ZIFENCEI_EXTENSION?=
# CPU specific compilation flags
CFLAGS_CPU+=-march=rv32i$(_M_EXTENSION)a$(_FPU_EXTENSION)c_zicsr$\
	$(_ZIFENCEI_EXTENSION)
CFLAGS_CPU+=-mabi=ilp32$(_FPU_EXTENSION) -Os
# RISC-V does not trap division by zero, enable the sanitizer to check those.
# With `-fsanitize-undefined-trap-on-error`, we lose a bit of specificity on the
# exact issue, but the added code is as small as it gets.
CFLAGS_CPU+=-fsanitize=integer-divide-by-zero -fsanitize-undefined-trap-on-error
LDFLAGS_EXTRA+=-mrelax

ifneq ($(CONFIG_LTO),)
CFLAGS_CPU+=-flto
LDFLAGS_EXTRA+=-flto
endif

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
	"$(clang_resource_dir)/lib/baremetal/libclang_rt.builtins-riscv32.a"
else
LDFLAGS_EXTRA+=-static-libgcc -lgcc
endif

core-y=cpu.o init.o panic.o task.o switch.o __builtin.o math.o
core-$(CONFIG_IT8XXX2_MUL_WORKAROUND)+=__it8xxx2_arithmetic.o
