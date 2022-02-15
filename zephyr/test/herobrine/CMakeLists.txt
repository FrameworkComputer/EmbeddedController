# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(herobrine)

zephyr_include_directories("${PLATFORM_EC}/zephyr/projects/herobrine/include")

target_sources_ifdef(CONFIG_TEST_BOARD_CHIPSET
	app PRIVATE src/board_chipset.c)
target_sources_ifdef(CONFIG_TEST_BOARD_CHIPSET
	app PRIVATE ${PLATFORM_EC}/zephyr/projects/herobrine/src/board_chipset.c)
