# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(kingler)

target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/projects/corsola/src/kingler/board_steelix.c)

target_sources(app PRIVATE src/fakes.c)

target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CONVERTIBLE
			app PRIVATE src/tablet.c)
target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CLAMSHELL
			app PRIVATE src/clamshell.c)
