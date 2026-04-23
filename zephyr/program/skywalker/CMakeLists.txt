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
elseif(DEFINED CONFIG_BOARD_DOOKU)
	project(dooku)
	add_subdirectory(dooku)
elseif(DEFINED CONFIG_BOARD_VADER)
	project(vader)
	add_subdirectory(vader)
elseif(DEFINED CONFIG_BOARD_JAINA)
	project(jaina)
	add_subdirectory(jaina)
elseif(DEFINED CONFIG_BOARD_R2D2)
	project(r2d2)
	add_subdirectory(r2d2)
elseif(DEFINED CONFIG_BOARD_SHEEV)
	project(sheev)
	add_subdirectory(sheev)
endif()
