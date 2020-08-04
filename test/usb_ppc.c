/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PD module.
 */
#include "common.h"
#include "console.h"
#include "crc.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usbc_ppc.h"
#include "util.h"

const struct ppc_drv null_drv = {
	.init = NULL,
	.is_sourcing_vbus = NULL,
	.vbus_sink_enable = NULL,
	.vbus_source_enable = NULL,
	.set_polarity = NULL,
	.set_vbus_source_current_limit = NULL,
	.discharge_vbus = NULL,
	.set_sbu = NULL,
	.set_vconn = NULL,
	.is_vbus_present = NULL,
	.enter_low_power_mode = NULL,
};

struct ppc_config_t ppc_chips[] = {
	[0] = {
		.drv = &null_drv
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const struct tcpc_config_t tcpc_config[] = {
	[0] = {
	},
};

static int test_ppc_init(void)
{
	int rv;

	rv = ppc_init(1);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_init(0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_is_sourcing_vbus(void)
{
	int rv;

	rv = ppc_is_sourcing_vbus(1);
	TEST_ASSERT(rv == 0);
	rv = ppc_is_sourcing_vbus(0);
	TEST_ASSERT(rv == 0);

	return EC_SUCCESS;
}

static int test_ppc_set_polarity(void)
{
	int rv;

	rv = ppc_set_polarity(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_set_polarity(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_set_vbus_source_current_limit(void)
{
	int rv;

	rv = ppc_set_vbus_source_current_limit(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_set_vbus_source_current_limit(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_set_sbu(void)
{
	int rv;

	rv = ppc_set_sbu(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_set_sbu(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_set_vconn(void)
{
	int rv;

	rv = ppc_set_vconn(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_set_vconn(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_discharge_vbus(void)
{
	int rv;

	rv = ppc_discharge_vbus(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_discharge_vbus(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_vbus_sink_enable(void)
{
	int rv;

	rv = ppc_vbus_sink_enable(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_vbus_sink_enable(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_enter_low_power_mode(void)
{
	int rv;

	rv = ppc_enter_low_power_mode(1);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_enter_low_power_mode(0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_vbus_source_enable(void)
{
	int rv;

	rv = ppc_vbus_source_enable(1, 0);
	TEST_ASSERT(rv == EC_ERROR_INVAL);
	rv = ppc_vbus_source_enable(0, 0);
	TEST_ASSERT(rv == EC_ERROR_UNIMPLEMENTED);

	return EC_SUCCESS;
}

static int test_ppc_is_vbus_present(void)
{
	int rv;

	rv = ppc_is_vbus_present(1);
	TEST_ASSERT(rv == 0);
	rv = ppc_is_vbus_present(0);
	TEST_ASSERT(rv == 0);

	return EC_SUCCESS;
}



void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_ppc_init);
	RUN_TEST(test_ppc_is_sourcing_vbus);
	RUN_TEST(test_ppc_set_polarity);
	RUN_TEST(test_ppc_set_vbus_source_current_limit);
	RUN_TEST(test_ppc_set_sbu);
	RUN_TEST(test_ppc_set_vconn);
	RUN_TEST(test_ppc_discharge_vbus);
	RUN_TEST(test_ppc_vbus_sink_enable);
	RUN_TEST(test_ppc_enter_low_power_mode);
	RUN_TEST(test_ppc_vbus_source_enable);
	RUN_TEST(test_ppc_is_vbus_present);

	test_print_result();
}
