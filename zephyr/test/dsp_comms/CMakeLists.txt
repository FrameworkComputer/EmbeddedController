# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.24.0)
find_package(Zephyr REQUIRED HINTS "$ENV{ZEPHYR_BASE}")
project(cros.dsp)

target_sources(app PRIVATE test_main.cc)
target_link_libraries(app
  PRIVATE
    pw_unit_test.light
    pw_unit_test.logging_event_handler
)

target_link_libraries(app PRIVATE ${TEST_LIB}.lib)
zephyr_append_cmake_library(${TEST_LIB})
