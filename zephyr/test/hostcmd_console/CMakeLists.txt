# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(hostcmd_console)

zephyr_include_directories(include)

target_sources(app PRIVATE ../../../chip/host/lpc.c)

target_sources(app PRIVATE src/hostcmd_console.c)
target_sources(app PRIVATE src/main.c)
