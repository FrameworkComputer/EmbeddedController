# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(guybrush)

zephyr_library_include_directories(include)

set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/guybrush" CACHE PATH
  "Path to the platform/ec baseboard directory")
set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/guybrush" CACHE PATH
  "Path to the platform/ec board directory")
