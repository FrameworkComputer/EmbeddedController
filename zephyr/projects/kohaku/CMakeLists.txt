# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

# Append ${PLATFORM_EC}/zephyr/include/drivers to SYSCALL_INCLUDE_DIRS
# variable for generating __syscall api functions by the
# `gen_syscalls.py` script.
# TODO: remove this when zmake support
list(APPEND
     SYSCALL_INCLUDE_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../../include/drivers"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kohaku)

zephyr_include_directories(include)
