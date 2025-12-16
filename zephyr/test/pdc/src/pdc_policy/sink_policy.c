/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file tests the sink policies on type-C ports.
 */

#include "chipset.h"
#include "emul/emul_pdc.h"
#include "test/util.h"
#include "timer.h"
#include "usbc/pdc_power_mgmt.h"
#include "usbc/utils.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_sink_policy);

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, sniff_pdc_set_sink_path, const struct device *, bool);

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == 2,
	     "PDC sink policy test suite must supply exactly 2 PDC ports");

#define PDC_TEST_TIMEOUT 2000

/* TODO: b/343760437 - Once the emulator can detect the PDC threads are idle,
 * remove the sleep delay to let the policy code run.
 */
#define PDC_POLICY_DELAY_MS 500
#define PDC_NODE_PORT0 DT_NODELABEL(pdc_emul1)
#define PDC_NODE_PORT1 DT_NODELABEL(pdc_emul2)

#define USBC_NODE0 DT_NODELABEL(usbc0)
#define USBC_NODE1 DT_NODELABEL(usbc1)

#define TEST_USBC_PORT0 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT0)
#define TEST_USBC_PORT1 USBC_PORT_FROM_PDC_DRIVER_NODE(PDC_NODE_PORT1)

#define IS_ONE_BIT_SET IS_POWER_OF_TWO

static uint8_t sink_path_en_mask;

static void clear_partner_pdos(const struct emul *e, enum pdo_type_t type)
{
	uint32_t clear_pdos[PDO_MAX_OBJECTS] = { 0 };

	emul_pdc_set_pdos(e, type, PDO_OFFSET_0, ARRAY_SIZE(clear_pdos),
			  PARTNER_PDO, clear_pdos);
}

struct pdc_fixture {
	const struct device *dev;
	const struct emul *emul_pdc;
	uint32_t pdos[PDO_MAX_OBJECTS];
	uint8_t port;
};

struct sink_policy_fixture {
	struct pdc_fixture pdc[CONFIG_USB_PD_PORT_MAX_COUNT];
};

static enum chipset_state_mask fake_chipset_state = CHIPSET_STATE_ON;

static int custom_fake_chipset_in_state(int mask)
{
	return !!(fake_chipset_state & mask);
}

static struct sink_policy_fixture fixture = {
	.pdc = {
		[0] = {
			.dev = DEVICE_DT_GET(PDC_NODE_PORT0),
			.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT0),
			.port = TEST_USBC_PORT0,
			.pdos = {
				PDO_FIXED(5000, 1500, 0),
				PDO_FIXED(9000, 3000, 0),
				PDO_FIXED(12000, 3000, 0),
			},
		},
		[1] = {
			.dev = DEVICE_DT_GET(PDC_NODE_PORT1),
			.emul_pdc = EMUL_DT_GET(PDC_NODE_PORT1),
			.port = TEST_USBC_PORT1,
			.pdos = {
				PDO_FIXED(5000, 1500, 0),
				PDO_FIXED(9000, 3000, 0),
				PDO_FIXED(20000, 5000, 0),
			},
		},
	},
};

static int pdc_dev_to_port(const struct device *dev)
{
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		if (dev == fixture.pdc[port].dev)
			return fixture.pdc[port].port;
	}

	/* LCOV_EXCL_START */
	zassert_true(0, "Unable to find port");

	return -1;
	/* LCOV_EXCL_STOP */
}

static int custom_fake_pdc_set_sink_path(const struct device *dev, bool en)
{
	int port = pdc_dev_to_port(dev);

	WRITE_BIT(sink_path_en_mask, port, en);
	LOG_INF("FAKE C%d: pdc_set_sink_path en_mask=0x%X", port,
		sink_path_en_mask);

	zassert_true(IS_ONE_BIT_SET(sink_path_en_mask) ||
		     sink_path_en_mask == 0);

	return pdc_set_sink_path(dev, en);
}

static void *sink_policy_setup(void)
{
	return &fixture;
};

static int connect_sink(const struct pdc_fixture *pdc)
{
	union connector_status_t cs = { 0 };

	emul_pdc_configure_snk(pdc->emul_pdc, &cs);
	clear_partner_pdos(pdc->emul_pdc, SOURCE_PDO);
	zassert_ok(emul_pdc_set_pdos(pdc->emul_pdc, SOURCE_PDO, PDO_OFFSET_0,
				     ARRAY_SIZE(pdc->pdos), PARTNER_PDO,
				     pdc->pdos));

	emul_pdc_set_rdo(pdc->emul_pdc, RDO_FIXED(1, 1500, 1500, 0));

	zassert_ok(emul_pdc_connect_partner(pdc->emul_pdc, &cs));
	zassert_ok(pdc_power_mgmt_wait_for_sync(pdc->port, 5000));

	return 0;
}

static void sink_policy_before(void *f)
{
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(sniff_pdc_set_sink_path);

	sink_path_en_mask = 0;

	chipset_in_state_fake.custom_fake = custom_fake_chipset_in_state;
	sniff_pdc_set_sink_path_fake.custom_fake =
		custom_fake_pdc_set_sink_path;
}

static void sink_policy_after(void *f)
{
	struct sink_policy_fixture *fixture = f;

	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		zassert_ok(emul_pdc_disconnect(fixture->pdc[port].emul_pdc));
		zassert_ok(emul_pdc_reset(fixture->pdc[port].emul_pdc));
		zassert_ok(pdc_power_mgmt_wait_for_sync(fixture->pdc[port].port,
							-1));
	}
}

ZTEST_SUITE(sink_policy, NULL, sink_policy_setup, sink_policy_before,
	    sink_policy_after, NULL);

static void test_sink_policy_helper(struct sink_policy_fixture *fixture,
				    unsigned int port_num)
{
	union connector_status_t connector_status;
	uint32_t rdo;

	zassert_true(port_num < ARRAY_SIZE(fixture->pdc));

	connect_sink(&fixture->pdc[port_num]);

	zassert_ok(pdc_power_mgmt_get_connector_status(port_num,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected on PORT0 */
	zassert_ok(emul_pdc_get_rdo(fixture->pdc[port_num].emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	/* Check charge manager seeding */
	zassert_true(TEST_WAIT_FOR(charge_manager_get_active_charge_port() ==
					   port_num,
				   PDC_TEST_TIMEOUT));

	int charge_manager_mv = charge_manager_get_charger_voltage();
	int charge_manager_ma = charge_manager_get_charger_current();
	int charge_manager_mw = (charge_manager_mv * charge_manager_ma) / 1000;

	zassert_true(
		charge_manager_mv <= CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		"Board max voltage is %umV, but charge manager seeded with %umV",
		CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV, charge_manager_mv);
	zassert_true(
		charge_manager_ma <= CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
		"Board max current is %umA, but charge manager seeded with %umA",
		CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA, charge_manager_ma);
	zassert_true(
		charge_manager_mw <= CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW,
		"Board max wattage is %umW, but charge manager seeded with %umW",
		CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW, charge_manager_mw);
}

/* Repeat test_sink_policy for two ports since they have different source cap
 * PDOs configured. */

ZTEST_USER_F(sink_policy, test_sink_policy_port0)
{
	test_sink_policy_helper(fixture, TEST_USBC_PORT0);
}

ZTEST_USER_F(sink_policy, test_sink_policy_port1)
{
	/* Ensure that the port 1 (high-power) source caps include a PDO that
	 * exceeds the board max current and wattage values to test clamping
	 */
	zassert_true(PDO_FIXED_CURRENT(fixture->pdc[1].pdos[2]) >
		     CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA);

	zassert_true((PDO_FIXED_CURRENT(fixture->pdc[1].pdos[2]) *
		      PDO_FIXED_VOLTAGE(fixture->pdc[1].pdos[2])) >
		     (CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW * 1000));
	test_sink_policy_helper(fixture, TEST_USBC_PORT1);
}

ZTEST_USER_F(sink_policy, test_sink_policy_set_rdo_fails)
{
	union connector_status_t connector_status;
	uint32_t rdo;

	/* Force emulator to not adopt the RDO we set. This is analogous to the
	 * port partner not agreeing to our RDO request. pdc_power_mgmt should
	 * time out and assume the RDO reported by the PDC */
	emul_pdc_set_feature_flag(fixture->pdc[TEST_USBC_PORT0].emul_pdc,
				  EMUL_PDC_FEATURE_DONT_APPLY_RDO);

	/* This forces the emulator's reported RDO to have RDO_POS()==1 */
	connect_sink(&fixture->pdc[TEST_USBC_PORT0]);

	zassert_ok(pdc_power_mgmt_get_connector_status(TEST_USBC_PORT0,
						       &connector_status));
	zassert_ok(pdc_power_mgmt_get_rdo(TEST_USBC_PORT0, &rdo));

	/* Verify original RDO is selected on PORT0 */
	zassert_equal(RDO_POS(connector_status.rdo), 1);
	zassert_equal(RDO_POS(rdo), 1);
}

ZTEST_USER_F(sink_policy, test_sink_policy_attach_better_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT0];
	const struct pdc_fixture *better_charger =
		&fixture->pdc[TEST_USBC_PORT1];

	connect_sink(charger);
	connect_sink(better_charger);

	/* Verify charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify better charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(better_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(better_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	/* Verify correct RDO is selected for disabled charger */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_attach_worse_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT1];
	const struct pdc_fixture *worse_charger =
		&fixture->pdc[TEST_USBC_PORT0];

	connect_sink(charger);
	connect_sink(worse_charger);

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify worse charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(worse_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	/* Verify correct RDO is selected for worse charger although not
	 * enabled. */
	zassert_ok(emul_pdc_get_rdo(worse_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_detach_better_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT0];
	const struct pdc_fixture *better_charger =
		&fixture->pdc[TEST_USBC_PORT1];

	connect_sink(charger);
	connect_sink(better_charger);

	/* Verify charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	/* Verify better charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(better_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(better_charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);

	zassert_ok(emul_pdc_disconnect(better_charger->emul_pdc));
	/* Detach doesn't call set_sink_path, update the test register
	 * manually.*/
	WRITE_BIT(sink_path_en_mask, better_charger->port, 0);
	zassert_ok(pdc_power_mgmt_wait_for_sync(better_charger->port, -1));
	zassert_ok(pdc_power_mgmt_wait_for_sync(charger->port, -1));

	/* Verify charger sink path is enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}

ZTEST_USER_F(sink_policy, test_sink_policy_detach_worse_charger)
{
	union connector_status_t connector_status;
	uint32_t rdo;
	const struct pdc_fixture *charger = &fixture->pdc[TEST_USBC_PORT1];
	const struct pdc_fixture *worse_charger =
		&fixture->pdc[TEST_USBC_PORT0];

	connect_sink(charger);
	connect_sink(worse_charger);

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify worse charger sink path is disabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(worse_charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 0);

	zassert_ok(emul_pdc_disconnect(worse_charger->emul_pdc));
	zassert_ok(pdc_power_mgmt_wait_for_sync(worse_charger->port, -1));

	/* Verify charger sink path stays enabled */
	zassert_ok(pdc_power_mgmt_get_connector_status(charger->port,
						       &connector_status));
	zassert_equal(connector_status.connect_status, 1);
	zassert_equal(connector_status.power_direction, 0);
	zassert_equal(connector_status.sink_path_status, 1);

	/* Verify correct RDO is selected */
	zassert_ok(emul_pdc_get_rdo(charger->emul_pdc, &rdo));
	zassert_equal(RDO_POS(rdo), 3);
}
