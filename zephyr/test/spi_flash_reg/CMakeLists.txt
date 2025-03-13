# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(spi_flash_reg)

set(PLATFORM_EC "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(PLATFORM_EC "${PLATFORM_EC}" REALPATH)

target_sources(testbinary
  PRIVATE
    src/main.c
    ${PLATFORM_EC}/common/spi_flash_reg.c
)

configure_file(
    ${PLATFORM_EC}/include/spi_flash_reg.h
    ${CMAKE_BINARY_DIR}/include/spi_flash_reg.h COPYONLY
)
target_include_directories(testbinary
  PRIVATE
    ${CMAKE_BINARY_DIR}/include include/
)

# W25Q64 has 0x400000 bytes as the first section, setting
# CONFIG_FLASH_SIZE_BYTES to that allows us to test every branch of
# spi_flash_reg.c
target_compile_definitions(testbinary
  PUBLIC
    CONFIG_SPI_FLASH_W25Q64
    CONFIG_FLASH_SIZE_BYTES=4194304
)
