# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(one_wire_uart)

# Include the test source and the file under test
target_sources_ifdef(CONFIG_PLATFORM_EC_ONE_WIRE_UART_KEYBOARD app PRIVATE
		src/main.c src/keyboard.c)
target_sources_ifdef(CONFIG_PLATFORM_EC_ONE_WIRE_UART_TABLET app PRIVATE
		src/tablet.c src/hid_i2c_touchpad.c)
