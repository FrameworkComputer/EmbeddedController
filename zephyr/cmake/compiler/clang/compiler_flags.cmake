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
    -Werror=array-bounds)
endif()

# clang flags for coverage generation
set_property(TARGET compiler PROPERTY coverage --coverage -fno-inline)

# Compiler flags for disabling position independent code / executable
set_compiler_property(PROPERTY no_position_independent
                      -fno-PIC
                      -fno-PIE
)
