# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(drivers)

add_subdirectory(common)

get_target_property(TEST_SOURCES app SOURCES)

# Support zmake for now
if("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers")
  set(CONFIG_LINK_TEST_SUITE_DEFAULT TRUE)
  set(CONFIG_LINK_TEST_SUITE_USB_MALFUNCTION_SINK TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_DEFAULT=1)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_USB_MALFUNCTION_SINK=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-ap_mux_control")
  set(CONFIG_LINK_TEST_SUITE_AP_MUX_CONTROL TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_AP_MUX_CONTROL=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-chargesplash")
  set(CONFIG_LINK_TEST_SUITE_CHARGESPLASH TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_CHARGESPLASH=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-isl923x")
  set(CONFIG_LINK_TEST_SUITE_ISL923X TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_ISL923X=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-led_driver")
  set(CONFIG_LINK_TEST_SUITE_LED_DRIVER TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_LED_DRIVER=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-pwm_led_driver")
  set(CONFIG_LINK_TEST_SUITE_PWM_LED_DRIVER TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_PWM_LED_DRIVER=1)
elseif("${ZMAKE_PROJECT_NAME}" STREQUAL "test-drivers-usbc_alt_mode")
  set(CONFIG_LINK_TEST_SUITE_USBC_ALT_MODE TRUE)
  add_compile_definitions(CONFIG_LINK_TEST_SUITE_USBC_ALT_MODE=1)
endif()

# Add linked suites here
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_DEFAULT default)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_AP_MUX_CONTROL ap_mux_control)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_CHARGESPLASH chargesplash)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_ISL923X isl923x)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_LED_DRIVER led_driver)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_PWM_LED_DRIVER pwm_led_driver)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_USB_MALFUNCTION_SINK usb_malfunction_sink)
add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_USBC_ALT_MODE usbc_alt_mode)

get_target_property(TEST_SOURCES_NEW app SOURCES)

# Check to make sure at least one suite was added
list(REMOVE_ITEM TEST_SOURCES_NEW ${TEST_SOURCES})
if(NOT TEST_SOURCES_NEW)
  message(FATAL_ERROR "Invalid configuration, must add test sources")
endif()

set_compiler_property(APPEND PROPERTY coverage -O0)
