# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(krabby)

zephyr_include_directories("${PLATFORM_EC}/zephyr/projects/corsola/include")

target_sources(app PRIVATE
	src/charger_workaround.c
	src/stubs.c
	${PLATFORM_EC}/zephyr/projects/corsola/src/krabby/charger_workaround.c)
