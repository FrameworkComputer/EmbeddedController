/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/isl9241.h"
#include "driver/charger/isl9241_public.h"
#include "emul/emul_isl9241.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define ISL9241_NODE DT_NODELABEL(isl9241_emul)

struct isl9241_driver_fixture {
	const struct emul *isl9241_emul;
};

static void *isl9241_driver_setup(void)
{
	static struct isl9241_driver_fixture fix;

	fix.isl9241_emul = EMUL_DT_GET(ISL9241_NODE);

	return &fix;
}

ZTEST_SUITE(isl9241_driver, drivers_predicate_post_main, isl9241_driver_setup,
	    NULL, NULL, NULL);

ZTEST(isl9241_driver, test_input_current_limit)
{
	int input_current = 3000;
	int temp;

	zassert_ok(
		charger_set_input_current_limit(CHARGER_SOLO, input_current));
	zassert_ok(charger_get_input_current_limit(CHARGER_SOLO, &temp));
	zassert_equal(input_current, temp);
}

ZTEST(isl9241_driver, test_device_id)
{
	int id;

	zassert_ok(charger_device_id(&id));
	zassert_equal(id, 0x000E);
}

ZTEST(isl9241_driver, test_manuf_id)
{
	int id;

	zassert_ok(charger_manufacturer_id(&id));
	zassert_equal(id, 0x0049);
}

/*
 * Unforuntely, there is no "get frequency" API, so we'll directly compare
 * expected register contents for this test
 */
struct frequency_test {
	int khz;
	uint16_t reg;
};

struct frequency_test frequency_table[] = {
	{ .khz = 1420, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_1420KHZ },
	{ .khz = 1180, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_1180KHZ },
	{ .khz = 1020, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_1020KHZ },
	{ .khz = 890, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_890KHZ },
	{ .khz = 808, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_808KHZ },
	{ .khz = 724, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_724KHZ },
	{ .khz = 656, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ },
	{ .khz = 600, .reg = ISL9241_CONTROL1_SWITCHING_FREQ_600KHZ },
};

ZTEST_F(isl9241_driver, test_frequency)
{
	for (int i = 0; i < ARRAY_SIZE(frequency_table); i++) {
		uint16_t register_peek;
		struct frequency_test *t = &frequency_table[i];

		zassert_ok(charger_set_frequency(t->khz));
		register_peek = isl9241_emul_peek(fixture->isl9241_emul,
						  ISL9241_REG_CONTROL1);
		zassert_equal(
			(register_peek &
			 ISL9241_CONTROL1_SWITCHING_FREQ_MASK) >>
				7,
			t->reg,
			"Failed to see correct register for %d kHz (0x%04x)\n",
			t->khz, register_peek);
	}
}

ZTEST(isl9241_driver, test_options)
{
	/* We're free to set whatever we want in CONTROL0 15:0 */
	int option = ISL9241_CONTROL0_EN_CHARGE_PUMPS |
		     ISL9241_CONTROL0_EN_BYPASS_GATE;
	int temp;

	zassert_ok(charger_set_option(option));
	zassert_ok(charger_get_option(&temp));
	zassert_equal(option, temp);
}

ZTEST(isl9241_driver, test_inhibit_charge)
{
	int status;

	zassert_ok(charger_set_mode(CHARGE_FLAG_INHIBIT_CHARGE));
	zassert_ok(charger_get_status(&status));
	zassert_equal((status & CHARGER_CHARGE_INHIBITED),
		      CHARGER_CHARGE_INHIBITED);
}

ZTEST_F(isl9241_driver, test_por_reset)
{
	zassert_ok(charger_set_mode(CHARGE_FLAG_POR_RESET));
	zassert_equal(isl9241_emul_peek(fixture->isl9241_emul,
					ISL9241_REG_CONTROL3),
		      ISL9241_CONTROL3_DIGITAL_RESET);
}

ZTEST(isl9241_driver, test_current)
{
	int current = 4000;
	int temp;

	zassert_ok(charger_set_current(CHARGER_SOLO, current));
	zassert_ok(charger_get_current(CHARGER_SOLO, &temp));
	zassert_equal(temp, current);
}

ZTEST(isl9241_driver, test_voltage)
{
	int voltage = 12000;
	int temp;

	zassert_ok(charger_set_voltage(CHARGER_SOLO, voltage));
	zassert_ok(charger_get_voltage(CHARGER_SOLO, &temp));
	zassert_equal(temp, voltage);
}

ZTEST_F(isl9241_driver, test_vbus_voltage)
{
	int voltage = 5088; /* ADC is in 96 mV steps */
	int status;

	isl9241_emul_set_vbus(fixture->isl9241_emul, voltage);
	zassert_ok(charger_get_status(&status));
	zassert_equal((status & CHARGER_AC_PRESENT), CHARGER_AC_PRESENT);

	zassert_ok(charger_get_vbus_voltage(0, &status));
	zassert_equal(voltage, status);
}

ZTEST_F(isl9241_driver, test_vsys_voltage)
{
	int voltage = 9984; /* ADC is in 96 mV steps */
	int temp;

	isl9241_emul_set_vsys(fixture->isl9241_emul, voltage);
	zassert_ok(charger_get_vsys_voltage(0, &temp));
	zassert_equal(voltage, temp);
}

ZTEST(isl9241_driver, test_post_init)
{
	/* Note: function is a no-op for this chip */
	zassert_ok(charger_post_init());
}

ZTEST_F(isl9241_driver, test_ac_prochot)
{
	/* Test bounds settings for allowed currents */
	/* Note: AC currents are scaled by the default of 20 */
	int scale = 20 / CONFIG_CHARGER_SENSE_RESISTOR_AC;
	int cur = (ISL9241_AC_PROCHOT_CURRENT_MAX + 100) * scale;

	printf("cur %d ", cur);
	zassert_ok(isl9241_set_ac_prochot(CHARGER_SOLO, cur));
	printf("%d\n", isl9241_emul_peek(fixture->isl9241_emul,
					 ISL9241_REG_AC_PROCHOT));
	zassert_equal(isl9241_emul_peek(fixture->isl9241_emul,
					ISL9241_REG_AC_PROCHOT),
		      AC_CURRENT_TO_REG(ISL9241_AC_PROCHOT_CURRENT_MAX));

	cur = (ISL9241_AC_PROCHOT_CURRENT_MIN - 100) * scale;
	printf("cur %d ", cur);
	zassert_ok(isl9241_set_ac_prochot(CHARGER_SOLO, cur));
	printf("%d\n", isl9241_emul_peek(fixture->isl9241_emul,
					 ISL9241_REG_AC_PROCHOT));
	zassert_equal(isl9241_emul_peek(fixture->isl9241_emul,
					ISL9241_REG_AC_PROCHOT),
		      AC_CURRENT_TO_REG(ISL9241_AC_PROCHOT_CURRENT_MIN));
}

ZTEST_F(isl9241_driver, test_dc_prochot)
{
	/* Test bounds settings for allowed currents */
	/* Note: DC currents are scaled by default of 10 */
	int scale = 10 / CONFIG_CHARGER_SENSE_RESISTOR;
	int cur = (ISL9241_DC_PROCHOT_CURRENT_MAX + 100) * scale;

	zassert_ok(isl9241_set_dc_prochot(CHARGER_SOLO, cur));
	zassert_equal(isl9241_emul_peek(fixture->isl9241_emul,
					ISL9241_REG_DC_PROCHOT),
		      ISL9241_DC_PROCHOT_CURRENT_MAX);

	cur = (ISL9241_DC_PROCHOT_CURRENT_MIN - 100) * scale;
	zassert_ok(isl9241_set_dc_prochot(CHARGER_SOLO, cur));
	zassert_equal(isl9241_emul_peek(fixture->isl9241_emul,
					ISL9241_REG_DC_PROCHOT),
		      ISL9241_DC_PROCHOT_CURRENT_MIN);
}

ZTEST_F(isl9241_driver, test_dump_registers)
{
	const struct shell *cli;
	const char *output;
	size_t output_size;
	const char dump_marker[] = "Dump ISL9241 registers";
	int rv;

	cli = get_ec_shell();
	shell_backend_dummy_clear_output(cli);

	/* Must define CONFIG_CMD_CHARGER_DUMP for this sub-command */
	rv = shell_execute_cmd(cli, "charger dump");

	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	output = shell_backend_dummy_get_output(cli, &output_size);
	/*
	 * Checking the exact register dump is not very interesting.
	 * Let's check if the output starts out reasonable.
	 */
	zassert_true(output_size >= sizeof(dump_marker));
	if (output_size >= sizeof(dump_marker)) {
		zassert_true(strstr(output, dump_marker) != NULL,
			     "Expected: \"%s\" in \"%s\"", dump_marker, output);
	}
}

ZTEST(isl9241_driver, test_prochot_dump)
{
	/*
	 * Note: this function's purpose is to print register contents to the
	 * console for debugging
	 */
	print_charger_prochot(CHARGER_SOLO);
}
