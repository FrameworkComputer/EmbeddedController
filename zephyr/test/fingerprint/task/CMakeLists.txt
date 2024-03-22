# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fpsensor_task_test)

target_sources(app PRIVATE src/mock_fingerprint_algorithm.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_CAPTURE app PRIVATE
	src/fpsensor_capture.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_ENROLL app PRIVATE
	src/fpsensor_enroll.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_FINGER_PRESENCE app PRIVATE
	src/fpsensor_finger_presence.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_INIT app PRIVATE
	src/fpsensor_init.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_MATCH app PRIVATE
	src/fpsensor_match.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_SHIM app PRIVATE
	src/fpsensor_shim.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_TEMPLATE app PRIVATE
	src/fpsensor_template.c)
