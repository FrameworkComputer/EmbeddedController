# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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

add_executable(second_loader)
target_sources(second_loader PRIVATE
  main.c
  serial.c
  spi_flash.c
  crc32.c
  gpio.c
  ${PLATFORM_EC}/third_party/cmsis_4/startup_ARMCM4.S
  )

target_include_directories(second_loader PRIVATE
  "${PLATFORM_EC}/zephyr/shim/include"
  "${PLATFORM_EC}/zephyr/shim/chip/mchp/include"
  )
target_include_directories(second_loader PRIVATE "${zephyr_includes}")

target_compile_options(second_loader PRIVATE "${zephyr_compile_options}")
target_compile_definitions(second_loader PRIVATE
    __NO_SYSTEM_INIT=1
    __STACK_SIZE=0x000007FC
    __START=main
)
target_link_options(second_loader BEFORE PRIVATE
  -nostdlib
  -g
  -mthumb
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/final.map,-T${CMAKE_CURRENT_SOURCE_DIR}/second_loader.ld
  )

# Create the second loader binary, locate it the root of the build
# directory as it needs to be found by the flash_util script
set(second_loader_elf ${CMAKE_CURRENT_BINARY_DIR}/second_loader.elf)
set(second_loader_bin ${CMAKE_BINARY_DIR}/second_loader.bin)

add_custom_target(generate_second_loader
  COMMAND ${CMAKE_OBJCOPY} -O binary ${second_loader_elf} ${second_loader_bin}
  BYPRODUCTS ${second_loader_bin}
  DEPENDS second_loader
  )

add_dependencies(zephyr generate_second_loader)
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND ${PYTHON_EXECUTABLE} ${SOC_DIR}/${ARCH}/${SOC_FAMILY}/common/spigen/mec_spi_gen.py
  -i ${CMAKE_BINARY_DIR}/second_loader.bin
  -o ${CMAKE_BINARY_DIR}/second_loader_fw.bin
  -c ${CONFIG_MCHP_MEC_HEADER_CHIP}
  -s ${CONFIG_MCHP_MEC_HEADER_FLASH_SIZE}
  -f ${CONFIG_MCHP_MEC_HEADER_SPI_FREQ_MHZ}
  -r ${CONFIG_MCHP_MEC_HEADER_SPI_READ_MODE}
  --drvstr ${CONFIG_MCHP_MEC_HEADER_SPI_DRVSTR}
  --slewrate ${CONFIG_MCHP_MEC_HEADER_SPI_SLEW_RATE}
  --fill
  ${MCHP_HEADER_VERBOSE_OPTION}
)
