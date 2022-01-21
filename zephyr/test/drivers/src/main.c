/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include "ec_app_main.h"

extern void test_suite_battery(void);
extern void test_suite_cbi(void);
extern void test_suite_smart_battery(void);
extern void test_suite_thermistor(void);
extern void test_suite_temp_sensor(void);
extern void test_suite_bma2x2(void);
extern void test_suite_bc12(void);
extern void test_suite_bmi260(void);
extern void test_suite_bmi160(void);
extern void test_suite_tcs3400(void);
extern void test_suite_espi(void);
extern void test_suite_bb_retimer(void);
extern void test_suite_ln9310(void);
extern void test_suite_lis2dw12(void);
extern void test_suite_stm_mems_common(void);
extern void test_suite_isl923x(void);
extern void test_suite_usb_mux(void);
extern void test_suite_ppc_syv682c(void);
extern void test_suite_ppc_sn5s330(void);
extern void test_suite_cros_cbi(void);
extern void test_suite_tcpci(void);
extern void test_suite_ps8xxx(void);
extern void test_suite_integration_usb(void);
extern void test_suite_power_common(void);
extern void test_suite_power_common_no_tasks(void);
extern void test_suite_watchdog(void);

void test_main(void)
{
	/* Test suites to run before ec_app_main.*/
	test_suite_power_common_no_tasks();

	ec_app_main();

	/* Test suites to run after ec_app_main.*/
	test_suite_battery();
	test_suite_cbi();
	test_suite_smart_battery();
	test_suite_thermistor();
	test_suite_temp_sensor();
	test_suite_bma2x2();
	test_suite_bc12();
	test_suite_bmi260();
	test_suite_bmi160();
	test_suite_tcs3400();
	test_suite_espi();
	test_suite_bb_retimer();
	test_suite_ln9310();
	test_suite_lis2dw12();
	test_suite_stm_mems_common();
	test_suite_isl923x();
	test_suite_usb_mux();
	test_suite_ppc_sn5s330();
	test_suite_ppc_syv682c();
	test_suite_cros_cbi();
	test_suite_tcpci();
	test_suite_ps8xxx();
	test_suite_integration_usb();
	test_suite_power_common();
	test_suite_watchdog();
}
