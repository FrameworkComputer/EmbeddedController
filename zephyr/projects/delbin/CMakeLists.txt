# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(delbin)

set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/delbin" CACHE PATH
  "Path to the platform/ec board directory")
set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/volteer" CACHE PATH
  "Path to the platform/ec baseboard directory")

# Include board specific header files
zephyr_library_include_directories(
  include
  "${PLATFORM_EC_BASEBOARD}"
  "${PLATFORM_EC_BOARD}")

# Include selected EC source for the baseboard
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_POWERSEQ
  "${PLATFORM_EC_BASEBOARD}/power.c")
