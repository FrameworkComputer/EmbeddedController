# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_CXX_STANDARD 20)

project(uart_printf)

set(SOURCES
    ../../../common/uart_printf.c
    src/fakes.cc
    src/main.cc
)

# Create the unittest libraries and binary
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

target_sources(testbinary
  PRIVATE
    ../../../common/uart_printf.c
    src/fakes.cc
    src/main.cc
)

target_include_directories(testbinary
  PRIVATE
    include
)
