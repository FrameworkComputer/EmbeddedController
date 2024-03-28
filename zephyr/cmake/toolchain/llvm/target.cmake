# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER lld)
set(BINTOOLS llvm)

# Mapping of Zephyr architecture -> toolchain triple
set(CROSS_COMPILE_TARGET_posix        x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET_unit_testing x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET_x86          x86_64-pc-linux-gnu)

set(CROSS_COMPILE_TARGET          ${CROSS_COMPILE_TARGET_${ARCH}})

if("${ARCH}" STREQUAL "arm")
  if(DEFINED CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    # ARMV7_M_ARMV8_M_MAINLINE means that ARMv7-M or backward compatible ARMv8-M
    # processor is used.
    set(CROSS_COMPILE_TARGET armv7m-cros-eabi)
  elseif(DEFINED CONFIG_ARMV6_M_ARMV8_M_BASELINE)
    # ARMV6_M_ARMV8_M_BASELINE means that ARMv6-M or ARMv8-M supporting the
    # Baseline implementation processor is used.
    set(CROSS_COMPILE_TARGET arm-none-eabi)
  endif()
endif()

# CMAKE_{C, ASM, CXX}_COMPILER_TARGET is used by CMake to provide correct
# "--target" option to Clang and by Zephyr to determine which runtime library
# should be linked.
set(CMAKE_C_COMPILER_TARGET   ${CROSS_COMPILE_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${CROSS_COMPILE_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${CROSS_COMPILE_TARGET})

set(CC clang)

# TODO(b/286589977): Remove if() when hermetic host toolchain is added to fwsdk.
# Use non-hermetic host toolchain when running outside chroot.
if(EXISTS /etc/cros_chroot_version)
  set(CROSS_COMPILE "/usr/bin/${CROSS_COMPILE_TARGET}-")
endif()
