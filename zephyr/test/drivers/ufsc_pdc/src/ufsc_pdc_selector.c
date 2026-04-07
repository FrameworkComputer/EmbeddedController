/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/pdc_runtime_port_config.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* Define a fake driver for "zephyr,fake-pdc" */
#define DT_DRV_COMPAT zephyr_fake_pdc

static int fake_pdc_init(const struct device *dev)
{
	return 0;
}

#define DEFINE_FAKE_PDC(inst)                                                  \
	DEVICE_DT_INST_DEFINE(inst, fake_pdc_init, NULL, NULL, NULL,           \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_FAKE_PDC)

/* Devices defined in overlay */
#define TEST_PDC_0 DT_NODELABEL(test_pdc_0)
#define TEST_PDC_1 DT_NODELABEL(test_pdc_1)
#define TEST_PDC_2 DT_NODELABEL(test_pdc_2)

/* UFSC Values defined in overlay */
#define VAL_P0_A DT_NODELABEL(ufsc_pdc_p0_val_a)
#define VAL_P0_B DT_NODELABEL(ufsc_pdc_p0_val_b)
#define VAL_P1_A DT_NODELABEL(ufsc_pdc_p1_val_a)

/* Mock the CBI check function */
FAKE_VALUE_FUNC(bool, cros_cbi_ufsc_check_match, enum cbi_ufsc_value_id);

static enum cbi_ufsc_value_id active_match_id;

static bool mock_cbi_ufsc_check_match(enum cbi_ufsc_value_id value_id)
{
	return (value_id == active_match_id);
}

static void ufsc_pdc_before(void *data)
{
	ARG_UNUSED(data);
	RESET_FAKE(cros_cbi_ufsc_check_match);
	cros_cbi_ufsc_check_match_fake.custom_fake = mock_cbi_ufsc_check_match;
}

ZTEST_SUITE(ufsc_pdc, NULL, NULL, ufsc_pdc_before, NULL, NULL);

ZTEST(ufsc_pdc, test_port0_select_a)
{
	const struct device *dev;

	/* Simulate UFSC matching Value A for Port 0 */
	active_match_id = CBI_UFSC_VALUE_ID(VAL_P0_A);
	board_get_pdc_for_port(0, &dev);
	zassert_equal(dev, DEVICE_DT_GET(TEST_PDC_0),
		      "Port 0 should select Device 0 when UFSC matches A");
}

ZTEST(ufsc_pdc, test_port0_select_b)
{
	const struct device *dev;

	/* Simulate UFSC matching Value B for Port 0 */
	active_match_id = CBI_UFSC_VALUE_ID(VAL_P0_B);
	board_get_pdc_for_port(0, &dev);
	zassert_equal(dev, DEVICE_DT_GET(TEST_PDC_1),
		      "Port 0 should select Device 1 when UFSC matches B");
}

ZTEST(ufsc_pdc, test_port0_no_match)
{
	const struct device *dev = (void *)0x12345678; /* Init to garbage */

	/* Simulate match ID that doesn't exist in Port 0 options */
	active_match_id = 9999;
	board_get_pdc_for_port(0, &dev);
	zassert_is_null(dev,
			"Port 0 should return NULL if no UFSC match found");
}

ZTEST(ufsc_pdc, test_port1_select)
{
	const struct device *dev;

	/* Simulate UFSC matching Value A for Port 1 */
	active_match_id = CBI_UFSC_VALUE_ID(VAL_P1_A);
	board_get_pdc_for_port(1, &dev);
	zassert_equal(dev, DEVICE_DT_GET(TEST_PDC_2),
		      "Port 1 should select Device 2 when UFSC matches A");
}
