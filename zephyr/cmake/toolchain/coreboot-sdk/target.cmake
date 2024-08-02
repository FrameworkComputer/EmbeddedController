# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Coreboot SDK uses GCC
set(COMPILER gcc)
set(LINKER ld)
set(BINTOOLS gnu)

# Mapping of Zephyr architecture -> coreboot-sdk toolchain
set(CROSS_COMPILE_TARGET_arm    arm-eabi)
set(CROSS_COMPILE_TARGET_riscv  riscv64-elf)
set(CROSS_COMPILE_TARGET_x86    i386-elf)

set(CROSS_COMPILE_TARGET        ${CROSS_COMPILE_TARGET_${ARCH}})

if("${ARCH}" STREQUAL "arm" AND CONFIG_ARM64)
  set(CROSS_COMPILE_TARGET      aarch64-elf)
elseif("${ARCH}" STREQUAL "x86" AND CONFIG_X86_64)
  set(CROSS_COMPILE_TARGET      x86_64-elf)
endif()

if(NOT DEFINED COREBOOT_SDK_ROOT)
  set(COREBOOT_SDK_ROOT "$ENV{COREBOOT_SDK_ROOT}")
endif()

set(CC gcc)
set(C++ g++)
set(TOOLCHAIN_HOME "${COREBOOT_SDK_ROOT}/bin/")
set(CROSS_COMPILE "${CROSS_COMPILE_TARGET}-")

set(CMAKE_AR         "${TOOLCHAIN_HOME}/${CROSS_COMPILE}ar")
set(CMAKE_NM         "${TOOLCHAIN_HOME}/${CROSS_COMPILE}nm")
set(CMAKE_OBJCOPY    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}objcopy")
set(CMAKE_OBJDUMP    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}objdump")
set(CMAKE_RANLIB     "${TOOLCHAIN_HOME}/${CROSS_COMPILE}ranlib")
set(CMAKE_READELF    "${TOOLCHAIN_HOME}/${CROSS_COMPILE}readelf")

# On ARM, we don't use libgcc: It's built against a fixed target (e.g.
# used instruction set, ABI, ISA extensions) and doesn't adapt when
# compiler flags change any of these assumptions. Use our own mini-libgcc
# instead.
if("${ARCH}" STREQUAL "arm")
  set(no_libgcc True)
endif()
