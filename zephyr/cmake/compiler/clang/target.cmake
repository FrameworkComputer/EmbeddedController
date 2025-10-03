# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/target.cmake")

# TODO(b/407786163): Fix the path reported by "--print-libgcc-file-name" so that
# this is not necessary.
if("${ARCH}" STREQUAL "riscv")
  # Provide a custom "compiler_set_linker_properties" that overrides the
  # default in upstream Zephyr (cmake/compiler/target_template.cmake).
  function(compiler_set_linker_properties)
    execute_process(
      COMMAND ${CMAKE_C_COMPILER}
      ${target_flag}
      --print-libgcc-file-name
      OUTPUT_VARIABLE libgcc_file_name
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )

    # libgcc_file_name is something like:
    # /usr/lib64/clang/21/lib/riscv32-cros-unknown-elf/libclang_rt.builtins.a
    cmake_path(SET rtlib_dir NORMALIZE "${libgcc_file_name}/../../baremetal")

    set_linker_property(PROPERTY lib_include_dir "-L${rtlib_dir}")
    set_linker_property(PROPERTY rt_library "-lclang_rt.builtins-riscv32")
  endfunction()
endif()

set(CMAKE_C_COMPILER "${CROSS_COMPILE}clang")
set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}clang++")
