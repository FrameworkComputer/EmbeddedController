# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

# USAGE FAULT on int overflow
zephyr_compile_options(-ftrapv)

if (DEFINED CONFIG_BOARD_BLOONCHIPPER)
	add_subdirectory(bloonchipper)
	project(bloonchipper)
endif()
