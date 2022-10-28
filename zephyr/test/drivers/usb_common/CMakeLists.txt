# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(usb_flags)

target_include_directories(app PRIVATE "include")

target_sources(app PRIVATE "src/suite.c" "src/usb_common.c")
target_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_RUNTIME_FLAGS app PRIVATE
	"src/usb_pd_flags.c")
target_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DISCHARGE app PRIVATE
	"src/usb_pd_discharge.c")
