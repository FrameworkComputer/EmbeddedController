# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/i2c.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
target_sources_ifndef(CONFIG_BOARD_SKYWALKER_SINGLE_USB_TYPEC app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/dp_alt_mode.c)

target_sources_ifdef(CONFIG_BOARD_SKYWALKER_PMIC_RESETB_IRQ app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/pmic_resetb_irq.c)

if(DEFINED CONFIG_BOARD_SKYWALKER)
	project(skywalker)
	add_subdirectory(skywalker)
elseif(DEFINED CONFIG_BOARD_LUUKE)
	project(luuke)
	add_subdirectory(luuke)
elseif(DEFINED CONFIG_BOARD_OBIWAN)
	project(obiwan)
	add_subdirectory(obiwan)
elseif(DEFINED CONFIG_BOARD_YODA)
	project(yoda)
	add_subdirectory(yoda)
elseif(DEFINED CONFIG_BOARD_ANAKIN)
	project(anakin)
	add_subdirectory(anakin)
elseif(DEFINED CONFIG_BOARD_BAZE)
	project(baze)
	add_subdirectory(baze)
elseif(DEFINED CONFIG_BOARD_TARKIN)
	project(tarkin)
	add_subdirectory(tarkin)
elseif(DEFINED CONFIG_BOARD_PADME)
	project(padme)
	add_subdirectory(padme)
elseif(DEFINED CONFIG_BOARD_GROGU)
	project(grogu)
	add_subdirectory(grogu)
endif()
