/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "crc8.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define GPIO_BATT_PRES_ODL_PATH NAMED_GPIOS_GPIO_NODE(ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

FAKE_VALUE_FUNC(int, battery2_write_func, const struct emul *, int, uint8_t,
		int, void *);
FAKE_VALUE_FUNC(int, battery2_read_func, const struct emul *, int, uint8_t *,
		int, void *);

bool authenticate_battery_type(int index, const char *manuf_name);

extern int battery_fuel_gauge_type_override;

struct battery_fixture {
	struct i2c_common_emul_data *battery_i2c_common;
	i2c_common_emul_finish_write_func finish_write_func;
	i2c_common_emul_start_read_func start_read_func;
};

static void *battery_setup(void)
{
	static struct battery_fixture fixture;
	static const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(battery));

	fixture.battery_i2c_common =
		emul_smart_battery_get_i2c_common_data(emul);

	return &fixture;
}

static void battery_before(void *data)
{
	struct battery_fixture *fixture = data;

	RESET_FAKE(battery2_write_func);
	RESET_FAKE(battery2_read_func);
	fixture->finish_write_func = fixture->battery_i2c_common->finish_write;
	fixture->start_read_func = fixture->battery_i2c_common->start_read;
}

static void battery_after(void *data)
{
	struct battery_fixture *fixture = data;
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	/* Set default state (battery is present) */
	gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0);
	battery_fuel_gauge_type_override = -1;

	i2c_common_emul_set_write_func(fixture->battery_i2c_common, NULL, NULL);
	i2c_common_emul_set_read_func(fixture->battery_i2c_common, NULL, NULL);
	fixture->battery_i2c_common->finish_write = fixture->finish_write_func;
	fixture->battery_i2c_common->start_read = fixture->start_read_func;
}

ZTEST_SUITE(battery, drivers_predicate_post_main, battery_setup, battery_before,
	    battery_after, NULL);

ZTEST_USER(battery, test_battery_is_present_gpio)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));

	zassert_not_null(dev, NULL);
	/* ec_batt_pres_odl = 0 means battery present. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 0));
	zassert_equal(BP_YES, battery_is_present());
	/* ec_batt_pres_odl = 1 means battery missing. */
	zassert_ok(gpio_emul_input_set(dev, GPIO_BATT_PRES_ODL_PORT, 1));
	zassert_equal(BP_NO, battery_is_present());
}

ZTEST(battery, test_authenticate_battery_type)
{
	/* Invalid index */
	zassert_false(authenticate_battery_type(BATTERY_TYPE_COUNT, NULL));
	/* Use fuel-gauge 1's manufacturer name for index 0 */
	zassert_false(
		authenticate_battery_type(0, board_battery_info[1].manuf_name));
	/* Use the correct manufacturer name, but wrong device name (because the
	 * index is 1 and not 0)
	 */
	zassert_false(
		authenticate_battery_type(1, board_battery_info[1].manuf_name));
}

ZTEST(battery, test_board_get_default_battery_type)
{
	zassert_equal(DEFAULT_BATTERY_TYPE, board_get_default_battery_type());
}

ZTEST_F(battery, test_board_cutoff_actuates_driver)
{
	/* We check the return type because board_is_cut_off() is set outside of
	 * board_cut_off_battery() and may be changed by other factors.
	 */

	/* Setup error conditions for battery 1*/
	battery_fuel_gauge_type_override = 1;
	fixture->battery_i2c_common->finish_write = NULL;
	i2c_common_emul_set_write_func(fixture->battery_i2c_common,
				       battery2_write_func, NULL);

	/* Check that i2c error returns EC_RES_ERROR */
	battery2_write_func_fake.return_val = -1;
	zassert_equal(EC_RES_ERROR, board_cut_off_battery());

	/* Check for OK when i2c succeeds */
	battery2_write_func_fake.return_val = 0;
	zassert_ok(board_cut_off_battery());
}

ZTEST_F(battery, test_sleep)
{
	/* Check 1st battery (lgc,ac17a8m) */
	battery_fuel_gauge_type_override = 0;
	zassert_equal(EC_ERROR_UNIMPLEMENTED, battery_sleep_fuel_gauge());

	/* Check 2nd battery (panasonic,ap15l5j) */
	battery_fuel_gauge_type_override = 1;
	fixture->battery_i2c_common->finish_write = NULL;
	i2c_common_emul_set_write_func(fixture->battery_i2c_common,
				       battery2_write_func, NULL);
	zassert_ok(battery_sleep_fuel_gauge());
}

struct battery2_read_data {
	size_t count;
	const uint8_t *values;
};

static int battery2_read(const struct emul *target, int reg, uint8_t *val,
			 int bytes, void *d)
{
	struct battery2_read_data *data = d;

	if (bytes < data->count) {
		*val = data->values[bytes];
	}

	return 0;
}

ZTEST(battery, test_is_charge_fet_disabled__cfet_mask_is_0)
{
	battery_fuel_gauge_type_override = 2;
	zassert_equal(0, battery_is_charge_fet_disabled());
}

ZTEST_F(battery, test_is_charge_fet_disabled__i2c_error)
{
	/* Set the battery to battery 1 */
	battery_fuel_gauge_type_override = 1;

	/* Override the finish_write common callback since we don't actually
	 * want to be messing with the emulator.
	 */
	fixture->battery_i2c_common->finish_write = NULL;

	/* Set up an error condition for battery 1 to fail writing to i2c */
	battery2_write_func_fake.return_val = -1;
	i2c_common_emul_set_write_func(fixture->battery_i2c_common,
				       battery2_write_func, NULL);

	/* Verify the error */
	zassert_equal(-1, battery_is_charge_fet_disabled());
}

ZTEST_F(battery, test_is_charge_fet_disabled)
{
	static uint8_t values[] = { 0x20, 0x54, 0xFF };

	static struct battery2_read_data data = {
		/* assume no PEC for count. We'll add it later if we need it */
		.count = ARRAY_SIZE(values) - 1,
		.values = values,
	};

	/* TODO(b/279203401): This code should actually live in an API to tell
	 * the emulator what an expected response is, rather than in the test
	 */
	if (IS_ENABLED(CONFIG_SMBUS_PEC)) {
		uint8_t pec;

		/* make room for the R/W bit */
		const uint8_t addr_8bit = 0xb << 1;

		/* from the DT battery node, grab fet_reg_addr,
		 * default is 0x0 MFG
		 */
		const uint8_t reg =
			DT_PROP_OR(DT_NODELABEL(battery), fet_reg_addr, 0x0);

		/* copied from i2c_controller.c:platform_ec_i2c_read
		 *
		 * We start our CRC with the front part of the i2c packet,
		 * which is the host driving address+reg/cmd, and then
		 * redriving address with read bit. This goes into "out"
		 * Then we complete the CRC across the two data bytes.
		 * This is the target driving it back.
		 * Finally, we'll append PEC to the end of the data, also
		 * something the target would return back.
		 * See: SMBUS Specification 3.2 page 40, & Sec 6.5
		 * http://smbus.org/specs/SMBus_3_2_20220112.pdf
		 */

		const uint8_t out[3] = { addr_8bit, reg, addr_8bit | 1 };

		pec = cros_crc8(out, ARRAY_SIZE(out));
		pec = cros_crc8_arg(values, ARRAY_SIZE(values) - 1, pec);

		values[ARRAY_SIZE(values) - 1] = pec;
		data.count++; /* update for PEC */
	}

	/* Set up the fake read function */
	battery2_read_func_fake.custom_fake = battery2_read;
	i2c_common_emul_set_read_func(fixture->battery_i2c_common,
				      battery2_read_func, &data);

	/* Override the finish_write and start_read common callback since we
	 * don't actually want to be messing with the emulator.
	 */
	fixture->battery_i2c_common->finish_write = NULL;
	fixture->battery_i2c_common->start_read = NULL;

	int rv = battery_is_charge_fet_disabled();

	zassert_equal(1, rv, "RV=%x", rv);
}

ZTEST_F(battery, test_get_disconnect_state__fail_i2c_read)
{
	/* Use battery 0 */
	battery_fuel_gauge_type_override = 0;

	/* Configure i2c to fail on read */
	battery2_read_func_fake.return_val = -1;
	i2c_common_emul_set_read_func(fixture->battery_i2c_common,
				      battery2_read_func, NULL);

	/* Check for disconnect error */
	zassert_equal(BATTERY_DISCONNECT_ERROR, battery_get_disconnect_state());
}

ZTEST_F(battery, test_get_disconnect_state)
{
	uint8_t values[] = { 0x00, 0x20, 0xFF };
	struct battery2_read_data data = {
		/* assume no PEC for count. We'll add it later if we need it */
		.count = ARRAY_SIZE(values) - 1,
		.values = values,
	};

	/* Use battery 0 */
	battery_fuel_gauge_type_override = 0;

	/* TODO(b/279203401): This code should actually live in an API to tell
	 * the emulator what an expected response is, rather than in the test
	 */
	if (IS_ENABLED(CONFIG_SMBUS_PEC)) {
		uint8_t pec;

		/* make room for the R/W bit */
		const uint8_t addr_8bit = 0xb << 1;

		/* reg is normally 0x0 for manufacturer access, but it can
		 * come from elsewhere. use the device tree for this purpose
		 */
		uint8_t mfg_reg = DT_PROP_OR(DT_NODELABEL(battery),
					     ship_mode_reg_addr, 0x0);
		const uint8_t out[3] = { addr_8bit, mfg_reg, addr_8bit | 1 };

		pec = cros_crc8(out, ARRAY_SIZE(out));
		pec = cros_crc8_arg(values, 2, pec);

		values[2] = pec;
		data.count = 3; /* update for PEC */
	}

	/* Enable i2c reads and set them to always return 0x2000 */
	battery2_read_func_fake.custom_fake = battery2_read;
	i2c_common_emul_set_read_func(fixture->battery_i2c_common,
				      battery2_read_func, &data);

	int rv = battery_get_disconnect_state();

	zassert_equal(BATTERY_DISCONNECTED, rv, "RV=%x", rv);
}
