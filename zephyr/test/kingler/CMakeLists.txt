# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(kingler)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories("${PLATFORM_EC}/zephyr/projects/corsola/include")

target_sources(app PRIVATE src/fakes.c)

target_sources_ifdef(CONFIG_TEST_STEELIX_RUSTY
app PRIVATE ${PLATFORM_EC}/zephyr/projects/corsola/src/kingler/board_steelix.c)
target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CONVERTIBLE
			app PRIVATE src/tablet.c)
target_sources_ifdef(CONFIG_TEST_FORM_FACTOR_CLAMSHELL
			app PRIVATE src/clamshell.c)
target_sources_ifdef(CONFIG_VARIANT_CORSOLA_DB_DETECTION
app PRIVATE ${PLATFORM_EC}/zephyr/projects/corsola/src/variant_db_detection.c)
target_sources_ifdef(CONFIG_TEST_DB_DETECT_TYPEC
			app PRIVATE src/db_detect_typec.c)
target_sources_ifdef(CONFIG_TEST_DB_DETECT_HDMI
			app PRIVATE src/db_detect_hdmi.c)
target_sources_ifdef(CONFIG_TEST_DB_DETECT_NONE
			app PRIVATE src/db_detect_none.c)
target_sources_ifdef(CONFIG_TEST_ALT_SENSOR_PROBE
			app PRIVATE src/alt_sensor.c)
