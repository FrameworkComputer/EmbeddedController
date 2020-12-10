# Copyright 2020 The Chromium OS Authors. All rights reserved.
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

set(CC gcc)
set(CROSS_COMPILE "/opt/coreboot-sdk/bin/${CROSS_COMPILE_TARGET}-")

set(CMAKE_AR         "${CROSS_COMPILE}ar")
set(CMAKE_NM         "${CROSS_COMPILE}nm")
set(CMAKE_OBJCOPY    "${CROSS_COMPILE}objcopy")
set(CMAKE_OBJDUMP    "${CROSS_COMPILE}objdump")
set(CMAKE_RANLIB     "${CROSS_COMPILE}ranlib")
set(CMAKE_READELF    "${CROSS_COMPILE}readelf")
