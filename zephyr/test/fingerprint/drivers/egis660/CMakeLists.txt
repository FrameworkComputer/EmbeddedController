# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(egis660_test)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# Add fingerprint drivers directory to include fingerprint_egis660_pal.h
target_include_directories(app PUBLIC ${PLATFORM_EC}/zephyr/drivers/fingerprint)

# Include local config_chip.h
zephyr_include_directories(include)
