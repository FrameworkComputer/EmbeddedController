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

if (DEFINED CONFIG_TEST_SUITE_PDC_API)
	target_sources(app PRIVATE src/generic/pdc_api.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_GENERIC)
	target_sources(app PRIVATE src/generic/pdc_attached_snk.c)
	target_sources(app PRIVATE src/generic/pdc_power_mgmt_api.c)
	target_sources(app PRIVATE src/generic/pdc_pmc_debug_api.c)
	target_sources_ifdef(CONFIG_PDC_RUNTIME_PORT_CONFIG
		app PRIVATE src/board_runtime_config.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_UTILS)
	target_sources(app PRIVATE src/generic/usb_utils.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_POLICY)
	target_sources(app PRIVATE src/pdc_policy/non_pd_policy.c)
	target_sources(app PRIVATE src/pdc_policy/select_best_pdo.c)
	target_sources(app PRIVATE src/pdc_policy/src_policy_common.c)
	target_sources(app PRIVATE src/pdc_policy/src_policy_3a_port.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_POLICY_SRC_NO_3A)
	target_sources(app PRIVATE src/pdc_policy/src_policy_common.c)
	target_sources(app PRIVATE src/pdc_policy/src_policy_no_3a_port.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_POLICY_SINK)
	target_sources(app PRIVATE src/pdc_policy/sink_policy.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_BATTERY_STATUS)
	target_sources(app PRIVATE src/generic/pdc_battery_status.c)
endif()

if (DEFINED CONFIG_TEST_PDC_POLICY_DEAD_BATTERY_TWO_CHARGERS)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_policy.c)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_two_chargers.c)
endif()

if (DEFINED CONFIG_TEST_PDC_POLICY_DEAD_BATTERY_AC_ONLY_AP_ON)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_policy.c)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_ac_only_ap_on.c)
endif()

if (DEFINED CONFIG_TEST_PDC_POLICY_DEAD_BATTERY_AC_ONLY_AP_OFF)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_policy.c)
	target_sources(app PRIVATE src/pdc_policy/dead_battery_ac_only_ap_off.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_POLICY_CLAMPED)
	target_sources(app PRIVATE src/pdc_policy/select_best_pdo_clamped.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_POLICY_VARIABLE)
	target_sources(app PRIVATE src/pdc_policy/select_best_pdo_variable.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_API_NULL_CHECK)
	target_sources(app PRIVATE src/generic/pdc_null_api.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_DEVICE_NOT_READY)
	target_sources(app PRIVATE src/generic/device_not_ready.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_RTS54XX)
	target_sources(app PRIVATE src/rts54xx/rts54xx.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_TPS6699X)
	zephyr_include_directories(${PLATFORM_EC}/zephyr/drivers/usbc)
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

if (DEFINED CONFIG_TEST_SUITE_UCSI_HOSTCMD)
	target_sources(app PRIVATE src/generic/ucsi_host_cmd.c)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/subsys/ucsi/ucsi_host_cmd.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PPM)
	zephyr_include_directories(${PLATFORM_EC}/zephyr/subsys/ucsi)
	target_sources(app PRIVATE src/generic/ppm.c)
	target_sources_ifdef(CONFIG_PDC_RUNTIME_PORT_CONFIG
		app PRIVATE src/board_runtime_config.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PPM_CONSOLE)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/subsys/ucsi/ppm_console.c)
	target_sources(app PRIVATE src/generic/ppm_console.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_PMC_USB_MUX)
	target_sources(app PRIVATE src/generic/pdc_pmc_usb_mux.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PPM_DRIVER)
	target_sources(app PRIVATE src/generic/ppm_driver.c)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/subsys/ucsi/ppm_driver.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_UCSI)
	target_sources(app PRIVATE src/ucsi/ucsi.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PPM_STATE_MACHINE)
	target_sources(app PRIVATE src/generic/ppm_state_machine.c)

	zephyr_include_directories(${PLATFORM_EC}/zephyr/subsys/ucsi)
	target_sources(app PRIVATE ${PLATFORM_EC}/zephyr/subsys/ucsi/ppm_common.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_TRACE_MSG)
	target_sources(app PRIVATE src/generic/pdc_trace_msg.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_RUNTIME_PORT_CONFIG)
	target_sources_ifdef(CONFIG_TEST_PDC_RUNTIME_CONFIG_SCENARIO_NO_PDCS
		app PRIVATE src/runtime_config/no_pdcs.c)
	target_sources_ifdef(CONFIG_TEST_PDC_RUNTIME_CONFIG_SCENARIO_ONE_PDC
		app PRIVATE src/runtime_config/one_pdc.c)
	target_sources_ifdef(CONFIG_TEST_PDC_RUNTIME_CONFIG_SCENARIO_DISCONT
		app PRIVATE src/runtime_config/discontiguous.c)
	target_sources_ifdef(CONFIG_TEST_PDC_RUNTIME_CONFIG_SCENARIO_TWO_PDCS
		app PRIVATE src/runtime_config/two_pdcs.c)
endif()

if (DEFINED CONFIG_TEST_SUITE_PDC_COMMS_SUSPEND_RESUME)
	target_sources(app PRIVATE src/generic/pdc_comms_suspend_resume.c)
endif()
