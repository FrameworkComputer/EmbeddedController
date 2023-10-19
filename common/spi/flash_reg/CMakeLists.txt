# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

# Create the library
if(BOARD STREQUAL unit_testing)
  find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
  add_library(spi_flash_reg)
else()
  zephyr_library_named(spi_flash_reg)
  target_link_libraries(spi_flash_reg PRIVATE cros_ec_interface)
  # When LTO is enabled, enable only for the "app" library, which compiles
  # and links all Chromium OS sources.
  # TODO: Enable LTO for all sources when Zephyr supports it.
  # See https://github.com/zephyrproject-rtos/zephyr/issues/2112
  if (DEFINED CONFIG_LTO)
    # The Zephyr toolchain generates linker errors if both CONFIG_LTO and
    # CONFIG_FPU are used. See b/184302085.
    if(("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "zephyr") AND
     (DEFINED CONFIG_FPU))
      message(STATUS "Zephyr toolchain and CONFIG_FPU detected: disabling LTO")
    else()
      set_property(
        TARGET
          spi_flash_reg
        PROPERTY
          INTERPROCEDURAL_OPTIMIZATION True
      )
    endif()
  endif()
endif()

# Add the sources
target_sources(spi_flash_reg
  PRIVATE
    src/spi_flash_reg.c
  PUBLIC
    public/spi_flash_reg.h
)

# Include the public directory
target_include_directories(spi_flash_reg PUBLIC public)

if(BOARD STREQUAL unit_testing)
  project(flash_reg)

  # Set the sources for the test
  target_sources(testbinary PRIVATE src/spi_flash_reg_test.c)

  # Link the library being tested
  target_link_libraries(testbinary PRIVATE spi_flash_reg)

  # Add the private directory used to mock components of the EC
  target_include_directories(testbinary PRIVATE private)
  target_include_directories(spi_flash_reg PRIVATE private)

  # Zephyr's unittest also generates a test_interface which we need in the
  # library (not just in the test binary)
  target_link_libraries(spi_flash_reg PRIVATE test_interface)
  string(TOLOWER "${CMAKE_C_COMPILER_ID}" lowercase_compiler_id)
  if("${lowercase_compiler_id}" STREQUAL "clang")
    target_link_options(testbinary PRIVATE -fprofile-instr-generate)
  endif()

  # W25Q64 has 0x400000 bytes as the first section, setting
  # CONFIG_FLASH_SIZE_BYTES to that allows us to test every branch of
  # spi_flash_reg.c
  add_compile_definitions(spi_flash_reg
    PUBLIC
      CONFIG_SPI_FLASH_W25Q64
      CONFIG_FLASH_SIZE_BYTES=4194304
  )
endif()
