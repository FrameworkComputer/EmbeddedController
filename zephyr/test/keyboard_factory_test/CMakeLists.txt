# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(keyboard_factory_test)

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(app PRIVATE ../../../chip/host/lpc.c)

target_sources(app PRIVATE src/main.c)
