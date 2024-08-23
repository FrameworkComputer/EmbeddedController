/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "fpc_private.h"
#include "fpsensor_driver.h"
#include "test_util.h"

/* Hardware-dependent smoke test that makes a SPI transaction with the
 * fingerprint sensor.
 */
test_static int test_fp_check_hwid(void)
{
	/* All fingerprint sensor support exists exclusively in RW. */
	TEST_ASSERT(IS_ENABLED(SECTION_IS_RW));

#if defined(CONFIG_FP_SENSOR_FPC1025) || defined(CONFIG_FP_SENSOR_FPC1145)
	uint16_t id = 0;
	TEST_EQ(fpc_get_hwid(&id), EC_SUCCESS, "%d");
	/* The lower 4-bits of the sensor hardware id are a
	 * manufacturing ID that is ok to vary.
	 */
	TEST_EQ(FP_SENSOR_HWID, id >> 4, "0x%04x");
	return EC_SUCCESS;
#endif

#if defined(CONFIG_FP_SENSOR_ELAN80SG)
	uint16_t id = 0;
	TEST_EQ(elan_get_hwid(&id), EC_SUCCESS, "%d");
	TEST_EQ(FP_SENSOR_HWID, id, "0x%04x");
	return EC_SUCCESS;
#endif

	return EC_ERROR_UNKNOWN;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_fp_check_hwid);

	test_print_result();
}
