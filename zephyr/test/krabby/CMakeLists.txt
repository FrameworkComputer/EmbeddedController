# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(krabby)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(
	"${PLATFORM_EC_PROGRAM_DIR}/corsola/include"
	include)

target_sources(app PRIVATE
	src/stubs.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/src/ite_usbc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/src/ite_usb_pd_policy.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/src/usb_pd_policy.c)

target_sources_ifdef(CONFIG_TEST_KRABBY
	app PRIVATE
	src/charger_workaround.c
	src/main.c
	src/power_seq.c
	src/usb_mux_init.c
	src/usbc_config.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/src/ite_charger_workaround.c)

target_sources_ifdef(CONFIG_TEST_TENTACRUEL
	app PRIVATE
	src/fake.c
	src/ppc_tentacruel.c
	src/temp_tentacruel.c
	src/fwconfig_tentacruel.c
	src/usb_mux_tentacruel.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/tentacruel/src/ppc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/tentacruel/src/temp.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/tentacruel/src/sensor.c)

target_sources_ifdef(CONFIG_TEST_STARMIE
	app PRIVATE
	src/main.c
	src/power_seq.c
	src/temp_starmie.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/starmie/src/thermal.c
)

target_sources_ifdef(CONFIG_TEST_CHINCHOU
	app PRIVATE
	src/board_chinchou.c
	src/temp_chinchou.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/chinchou/src/board.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/chinchou/src/thermal.c
)

target_sources_ifdef(CONFIG_TEST_WOOBAT
	app PRIVATE
	src/fake.c
	src/ppc_woobat.c
	src/temp_woobat.c
	src/fwconfig_woobat.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/woobat/src/ppc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/woobat/src/temp.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/woobat/src/sensor.c)

target_sources_ifdef(CONFIG_TEST_WUGTRIO
	app PRIVATE
	src/ppc_wugtrio.c
	src/base_detect.c
	src/motionsense_wugtrio.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/wugtrio/src/ppc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/wugtrio/src/base_detect.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/wugtrio/src/alt_motionsense.c)

target_sources_ifdef(CONFIG_TEST_SKITTY
	app PRIVATE
	src/fake.c
	src/ppc_skitty.c
	src/temp_skitty.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/skitty/src/ppc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/skitty/src/temp.c)

target_sources_ifdef(CONFIG_TEST_VELUZA
	app PRIVATE
	src/fake.c
	src/ppc_veluza.c
	src/temp_veluza.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/veluza/src/ppc.c
	${PLATFORM_EC_PROGRAM_DIR}/corsola/veluza/src/temp.c)
