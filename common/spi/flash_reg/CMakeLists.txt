# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

# Create the library
if(BOARD STREQUAL unit_testing)
  add_library(spi_flash_reg)
else()
  zephyr_library_named(spi_flash_reg)
  target_link_libraries(spi_flash_reg PRIVATE cros_ec_interface)
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

  # Set the sources for the test (must be done before find_package)
  set(SOURCES src/spi_flash_reg_test.c)

  # Create the unittest libraries and binary
  find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

  # Link the library being tested
  target_link_libraries(testbinary PRIVATE spi_flash_reg)

  # Add the private directory used to mock components of the EC
  target_include_directories(testbinary PRIVATE private)
  target_include_directories(spi_flash_reg PRIVATE private)

  # Zephyr's unittest also generates a test_interface which we need in the
  # library (not just in the test binary)
  target_link_libraries(spi_flash_reg PRIVATE test_interface)

  # W25Q64 has 0x400000 bytes as the first section, setting
  # CONFIG_FLASH_SIZE_BYTES to that allows us to test every branch of
  # spi_flash_reg.c
  add_compile_definitions(spi_flash_reg
    PUBLIC
      CONFIG_SPI_FLASH_W25Q64
      CONFIG_FLASH_SIZE_BYTES=4194304
  )
endif()