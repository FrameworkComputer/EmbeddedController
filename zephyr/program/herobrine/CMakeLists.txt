# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

cros_ec_library_include_directories(include)

# Common Herobrine implementation
zephyr_library_sources(
  "src/board_chipset.c"
)

# Board specific implementation
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "src/usbc_config.c"
  "src/usb_pd_policy.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C
  "src/i2c.c")

if(DEFINED CONFIG_BOARD_EVOKER)
	project(evoker)
	zephyr_library_sources(
		"evoker/src/charger_override.c"
	)
elseif(DEFINED CONFIG_BOARD_HEROBRINE)
	project(herobrine)
	add_subdirectory(herobrine)
elseif(DEFINED CONFIG_BOARD_HOGLIN)
	project(hoglin)
elseif(DEFINED CONFIG_BOARD_VILLAGER)
	zephyr_library_sources(
		"villager/src/keyboard.c"
	)
	project(villager)
elseif(DEFINED CONFIG_BOARD_ZOGLIN)
	project(zoglin)
elseif(DEFINED CONFIG_BOARD_ZOMBIE)
	project(zombie)
endif()
