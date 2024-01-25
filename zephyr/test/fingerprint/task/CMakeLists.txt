# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fpsensor_task_test)

target_sources(app PRIVATE src/mock_fingerprint_algorithm.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_CAPTURE app PRIVATE
	src/fpsensor_capture.c)
