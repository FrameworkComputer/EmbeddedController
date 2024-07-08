# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(input_keyboard)

target_sources(app PRIVATE ../../../chip/host/lpc.c)

target_sources(app PRIVATE src/main.c)
