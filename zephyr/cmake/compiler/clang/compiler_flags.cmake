# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/compiler_flags.cmake")

# Disable -fno-freestanding.
set_compiler_property(PROPERTY hosted)

# When testing, look for stack smashing
if(DEFINED CONFIG_ZTEST AND DEFINED CONFIG_ARCH_POSIX)
add_compile_options(-fstack-protector-all)
endif()

if(DEFINED CONFIG_COMPILER_WARNINGS_AS_ERRORS)
  check_set_compiler_property(APPEND PROPERTY warning_extended -Wunused-variable
    -Werror=unused-variable -Werror=missing-braces
    -Werror=sometimes-uninitialized -Werror=unused-function
    -Werror=array-bounds -Werror=implicit-function-declaration
  )
endif()

# clang flags for coverage generation
set_property(TARGET compiler PROPERTY coverage --coverage -fno-inline)

# Compiler flags for disabling position independent code / executable
set_compiler_property(PROPERTY no_position_independent
                      -fno-PIC
                      -fno-PIE
)

# b/401022418 Newer versions of Clang produce some warnings. Until we have
# fixes for those, turn the warnings off.
set_compiler_property(APPEND PROPERTY warning_base
  -Wno-main
  -Wno-unknown-warning-option
  -Wno-arm-interrupt-vfp-clobber
  -Wno-extra
)

if("${ARCH}" STREQUAL "riscv")
  # TODO(b/409614368): The ChromeOS LLVM RISC-V baremetal toolchain is using the
  # wrong C++ header search path.
  set_property(TARGET compiler-cpp APPEND PROPERTY required "-nostdinc++")
  set_property(TARGET compiler-cpp APPEND PROPERTY required "-I/usr/${CROSS_COMPILE_TARGET}/usr/include/c++/v1/")
  # TODO(b/410086538): _GNU_SOURCE needs to be defined for libc++ built with
  # newlib as C library.
  set_property(TARGET compiler-cpp APPEND PROPERTY required "-D_GNU_SOURCE")
endif()
