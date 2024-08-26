# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS "$ENV{ZEPHYR_BASE}")
project(lid_angle_common)

target_sources(testbinary PRIVATE
  fakes.c
  main.c
  ../../../common/lid_angle_common.c
)
target_include_directories(testbinary PRIVATE
  include
  ../../../include
  ../../../fuzz
)
