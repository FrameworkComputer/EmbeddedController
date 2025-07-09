# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The NPCX monitor source needs the chip type and flash layout information
# provided by the board configuration. This is provided by
# zephyr/shim/include/config_chip.h, so we need all the include directories
# of the Zephyr application.
zephyr_get_include_directories_for_lang(C zephyr_includes STRIP_PREFIX)

# Something in the zephyr_get_compile_options_for_lang() output causes the
# "-imacros" option to get dropped during expansion when passed to
# target_compile_options().  Fetch the compile options directly from
# zephyr_interface which works as expected.
get_property(
  zephyr_compile_options
  TARGET zephyr_interface
  PROPERTY INTERFACE_COMPILE_OPTIONS
  )

add_executable(npcx_monitor npcx_monitor.c)
target_include_directories(npcx_monitor PRIVATE
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/zephyr/shim/chip/npcx/include"
  )
target_include_directories(npcx_monitor PRIVATE "${zephyr_includes}")
target_compile_options(npcx_monitor PRIVATE "${zephyr_compile_options}")

target_link_options(npcx_monitor BEFORE PRIVATE
  -nostdlib
  -g
  -mthumb
  -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/npcx_monitor.ld
  )

# Create the NPCX monitor binary, locate it the root of the build
# directory as it needs to be found by the flash_util script
set(npcx_monitor_elf ${CMAKE_CURRENT_BINARY_DIR}/npcx_monitor.elf)
set(npcx_monitor_bin ${CMAKE_BINARY_DIR}/npcx_monitor.bin)

if (NOT DEFINED CONFIG_COVERAGE)
add_custom_target(generate_npcx_monitor
  COMMAND ${CMAKE_OBJCOPY} -O binary ${npcx_monitor_elf} ${npcx_monitor_bin}
  BYPRODUCTS ${npcx_monitor_bin}
  DEPENDS npcx_monitor
  )
add_custom_target(check_monitor_hash
  COMMAND ${CMAKE_COMMAND} -DZEPHYR_TOOLCHAIN_VARIANT=${ZEPHYR_TOOLCHAIN_VARIANT}
  -DINPUT_FILE=${npcx_monitor_bin} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_hash.cmake
  DEPENDS ${npcx_monitor_bin}
  )

add_dependencies(zephyr check_monitor_hash)
endif()
