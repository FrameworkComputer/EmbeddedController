# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(pdc)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

zephyr_include_directories(include)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/fakes.c)

target_sources_ifdef(CONFIG_TEST_PDC_MESSAGE_TRACING  app PRIVATE
	src/util_pcap.c
	src/pdc_trace_msg.c)

if (DEFINED CONFIG_TEST_SUITE_PDC_GENERIC)
	target_sources(app PRIVATE src/generic/pdc_api.c)
	target_sources(app PRIVATE src/generic/pdc_power_mgmt_api.c)
	target_sources(app PRIVATE src/generic/pdc_pmc_debug_api.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_API_NULL_CHECK)
	target_sources(app PRIVATE src/generic/pdc_null_api.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_RTS54XX)
	target_sources(app PRIVATE src/rts54xx/rts54xx.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_TPS6699X)
	target_sources(app PRIVATE src/tps6699x/tps6699x.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_RETIMER_FW_UPDATE)
	target_sources(app PRIVATE src/generic/retimer_fw_update.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_CONSOLE)
	target_sources(app PRIVATE src/generic/pdc_console.c)
	target_sources(app PRIVATE src/mock_pdc_power_mgmt.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_HOSTCMD)
	target_sources(app PRIVATE src/generic/pdc_host_cmd.c)
	target_sources(app PRIVATE src/mock_pdc_power_mgmt.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_UCSI)
	target_sources(app PRIVATE src/ucsi/ucsi.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PPM)
	target_sources(app PRIVATE src/generic/ppm.c)

	zephyr_include_directories(${PLATFORM_EC}/zephyr/subsys/ucsi)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/subsys/ucsi/ppm_common.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_TRACE_MSG)
	target_sources(app PRIVATE src/generic/pdc_trace_msg.c)
endif()
