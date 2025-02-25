# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER lld)
set(BINTOOLS llvm)

# Look for toolchain binaries in /usr/bin
set(TOOLCHAIN_HOME "/usr/bin")

if("${ARCH}" STREQUAL "posix")
set(LINKER ld)
endif()

# Mapping of Zephyr architecture -> toolchain triple
set(CROSS_COMPILE_TARGET_posix        x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET_unit_testing x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET_x86          x86_64-pc-linux-gnu)

set(CROSS_COMPILE_TARGET          ${CROSS_COMPILE_TARGET_${ARCH}})

set(toolchain_uses_newlib FALSE)

if("${ARCH}" STREQUAL "arm")
  if(DEFINED CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    # ARMV7_M_ARMV8_M_MAINLINE means that ARMv7-M or backward compatible ARMv8-M
    # processor is used.
    #
    # The armv7m-cros-eabi compiler-rt was compiled with assumption that it will
    # run on Cortex-M4 MCU with enabled FPU. Calling builtin functions that use
    # floating-point instructions (e.g. __aeabi_ul2d) with FPU disabled leads to
    # Usage Fault. Let's check if FPU is enabled.
    if(NOT DEFINED CONFIG_FPU)
      message(FATAL_ERROR "The armv7m-cros-eabi toolchain requires enabled FPU")
    endif()
    set(CROSS_COMPILE_TARGET armv7m-cros-eabi)
  elseif(DEFINED CONFIG_ARMV6_M_ARMV8_M_BASELINE)
    # ARMV6_M_ARMV8_M_BASELINE means that ARMv6-M or ARMv8-M supporting the
    # Baseline implementation processor is used.
    set(CROSS_COMPILE_TARGET arm-none-eabi)
  endif()
  set(toolchain_uses_newlib TRUE)
elseif("${ARCH}" STREQUAL "riscv")
  set(CROSS_COMPILE_TARGET riscv32-cros-elf)
  set(toolchain_uses_newlib TRUE)
endif()

if(toolchain_uses_newlib)
  # LLVM based toolchains for ARM and RISC-V use newlib as a libc.
  # This variable is set AFTER all Kconfig files were processed, so it doesn't
  # affect them, but it's still useful for filtering tests.
  set(TOOLCHAIN_HAS_NEWLIB ON CACHE BOOL "True if toolchain supports newlib")
endif()

# LLVM_TOOLCHAIN_PATH is used as a base path to look for 'newlib.cfg' or
# 'picolibc.cfg' provided by toolchain. Our compiler doesn't provide these files
# but without this variable, CMake looks for these files starting from '/' which
# takes long time and can lead to errors if somebody creates the file somewhere.
set(LLVM_TOOLCHAIN_PATH "/usr/${CROSS_COMPILE_TARGET}")

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
