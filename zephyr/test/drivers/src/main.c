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
extern void test_suite_ppc(void);
extern void test_suite_bmi260(void);
extern void test_suite_bmi160(void);
extern void test_suite_tcs3400(void);

void test_main(void)
{
	/* Test suites to run before ec_app_main.*/

	ec_app_main();

	/* Test suites to run after ec_app_main.*/
	test_suite_battery();
	test_suite_cbi();
	test_suite_smart_battery();
	test_suite_thermistor();
	test_suite_temp_sensor();
	test_suite_bma2x2();
	test_suite_bc12();
	test_suite_ppc();
	test_suite_bmi260();
	test_suite_bmi160();
	test_suite_tcs3400();
}
