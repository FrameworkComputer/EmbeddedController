/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "charger.h"
#include "charge_ramp.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"

/* This test suite only works if the chg_chips array is not const. */
BUILD_ASSERT(IS_ENABLED(CONFIG_PLATFORM_EC_CHARGER_RUNTIME_CONFIG),
	     "chg_chips array cannot be const.");

/** Index of the charger chip we are overriding / working with. */
#define CHG_NUM (0)

/* FFF fakes for driver functions. These get assigned to members of the
 * charger_drv struct
 */
FAKE_VALUE_FUNC(enum ec_error_list, enable_otg_power, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, set_otg_current_voltage, int, int, int);
FAKE_VALUE_FUNC(int, is_sourcing_otg_power, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, get_actual_current, int, int *);
FAKE_VALUE_FUNC(enum ec_error_list, get_actual_voltage, int, int *);
FAKE_VALUE_FUNC(enum ec_error_list, set_voltage, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, get_vsys_voltage, int, int, int *);
FAKE_VALUE_FUNC(enum ec_error_list, enable_bypass_mode, int, bool);
FAKE_VALUE_FUNC(enum ec_error_list, set_vsys_compensation, int,
		struct ocpc_data *, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, is_icl_reached, int, bool *);
FAKE_VALUE_FUNC(enum ec_error_list, enable_linear_charge, int, bool);
FAKE_VALUE_FUNC(enum ec_error_list, get_battery_cells, int, int *);

/**
 * @brief If non-NULL, board_get_charger_chip_count returns the value this
 * pointer points to.
 */
static uint8_t *fake_charger_count;

/**
 * @brief Override of definition from common/charger.c. Allows adjusting the
 * number of chargers. This is not an FFF mock because FFF mock return values
 * default to 0 until the test code gets a change to update it, which can cause
 * a race condition as the EC initializes. This function has the correct
 * count as soon as the program starts, which is CHARGER_NUM chargers.
 *
 * @return uint8_t Number of charger chips
 */
uint8_t board_get_charger_chip_count(void)
{
	if (fake_charger_count) {
		return *fake_charger_count;
	}

	/* Default value */
	return CHARGER_NUM;
}

struct common_charger_mocked_driver_fixture {
	/* The original driver pointer that gets restored after the tests */
	const struct charger_drv *saved_driver_ptr;
	/* Mock driver that gets substituted */
	struct charger_drv mock_driver;
};

ZTEST(common_charger_mocked_driver, test_charger_enable_otg_power__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_enable_otg_power(-1, 0));
	zassert_equal(EC_ERROR_INVAL, charger_enable_otg_power(INT_MAX, 0));
}

ZTEST(common_charger_mocked_driver, test_charger_enable_otg_power__unimpl)
{
	/* enable_otg_power is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_enable_otg_power(CHG_NUM, 1));
}

ZTEST_F(common_charger_mocked_driver, test_charger_enable_otg_power)
{
	fixture->mock_driver.enable_otg_power = enable_otg_power;
	enable_otg_power_fake.return_val = 123;

	zassert_equal(enable_otg_power_fake.return_val,
		      charger_enable_otg_power(CHG_NUM, 1));

	zassert_equal(1, enable_otg_power_fake.call_count);
	zassert_equal(CHG_NUM, enable_otg_power_fake.arg0_history[0]);
	zassert_equal(1, enable_otg_power_fake.arg1_history[0]);
}

ZTEST(common_charger_mocked_driver,
      test_charger_set_otg_current_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL,
		      charger_set_otg_current_voltage(-1, 0, 0));
	zassert_equal(EC_ERROR_INVAL,
		      charger_set_otg_current_voltage(INT_MAX, 0, 0));
}

ZTEST(common_charger_mocked_driver,
      test_charger_set_otg_current_voltage__unimpl)
{
	/* set_otg_current_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_set_otg_current_voltage(CHG_NUM, 0, 0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_set_otg_current_voltage)
{
	fixture->mock_driver.set_otg_current_voltage = set_otg_current_voltage;
	set_otg_current_voltage_fake.return_val = 123;

	zassert_equal(set_otg_current_voltage_fake.return_val,
		      charger_set_otg_current_voltage(CHG_NUM, 10, 20));

	zassert_equal(1, set_otg_current_voltage_fake.call_count);
	zassert_equal(CHG_NUM, set_otg_current_voltage_fake.arg0_history[0]);
	zassert_equal(10, set_otg_current_voltage_fake.arg1_history[0]);
	zassert_equal(20, set_otg_current_voltage_fake.arg2_history[0]);
}

ZTEST(common_charger_mocked_driver, test_charger_is_sourcing_otg_power__invalid)
{
	/* is_sourcing_otg_power is NULL */
	zassert_equal(0, charger_is_sourcing_otg_power(0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_is_sourcing_otg_power)
{
	fixture->mock_driver.is_sourcing_otg_power = is_sourcing_otg_power;
	is_sourcing_otg_power_fake.return_val = 123;

	zassert_equal(is_sourcing_otg_power_fake.return_val,
		      charger_is_sourcing_otg_power(0));

	zassert_equal(1, is_sourcing_otg_power_fake.call_count);
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_current__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_get_actual_current(-1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_actual_current(INT_MAX, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_current__unimpl)
{
	/* get_actual_current is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_actual_current(CHG_NUM, NULL));
}

/**
 * @brief Custom fake for get_actual_current that can write to the output param
 */
static enum ec_error_list get_actual_current_custom_fake(int chgnum,
							 int *current)
{
	ARG_UNUSED(chgnum);

	*current = 1000;

	return EC_SUCCESS;
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_actual_current)
{
	int current;

	fixture->mock_driver.get_actual_current = get_actual_current;
	get_actual_current_fake.custom_fake = get_actual_current_custom_fake;

	zassert_equal(EC_SUCCESS,
		      charger_get_actual_current(CHG_NUM, &current));

	zassert_equal(1, get_actual_current_fake.call_count);
	zassert_equal(CHG_NUM, get_actual_current_fake.arg0_history[0]);
	zassert_equal(1000, current);
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_get_actual_voltage(-1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_actual_voltage(INT_MAX, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_actual_voltage__unimpl)
{
	/* get_actual_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_actual_voltage(CHG_NUM, NULL));
}

/**
 * @brief Custom fake for get_actual_voltage that can write to the output param
 */
static enum ec_error_list get_actual_voltage_custom_fake(int chgnum,
							 int *voltage)
{
	ARG_UNUSED(chgnum);

	*voltage = 2000;

	return EC_SUCCESS;
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_actual_voltage)
{
	int voltage;

	fixture->mock_driver.get_actual_voltage = get_actual_voltage;
	get_actual_voltage_fake.custom_fake = get_actual_voltage_custom_fake;

	zassert_equal(EC_SUCCESS,
		      charger_get_actual_voltage(CHG_NUM, &voltage));

	zassert_equal(1, get_actual_voltage_fake.call_count);
	zassert_equal(CHG_NUM, get_actual_voltage_fake.arg0_history[0]);
	zassert_equal(2000, voltage);
}

ZTEST(common_charger_mocked_driver, test_charger_set_voltage__invalid)
{
	/* charger number out of bounds */
	zassert_equal(EC_ERROR_INVAL, charger_set_voltage(-1, 0));
	zassert_equal(EC_ERROR_INVAL, charger_set_voltage(INT_MAX, 0));
}

ZTEST(common_charger_mocked_driver, test_charger_set_voltage__unimpl)
{
	/* set_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_set_voltage(CHG_NUM, 0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_set_voltage)
{
	fixture->mock_driver.set_voltage = set_voltage;
	set_voltage_fake.return_val = 123;

	zassert_equal(set_voltage_fake.return_val,
		      charger_set_voltage(CHG_NUM, 2000));

	zassert_equal(1, set_voltage_fake.call_count);
	zassert_equal(CHG_NUM, set_voltage_fake.arg0_history[0]);
	zassert_equal(2000, set_voltage_fake.arg1_history[0]);
}

ZTEST(common_charger_mocked_driver, test_charger_get_vsys_voltage__invalid)
{
	/* Cannot do chgnum bounds checking because
	 * charger_get_valid_chgnum() will convert chgnum to 0 unless
	 * CONFIG_CHARGER_SINGLE_CHIP is turned off.
	 */

	/* get_vsys_voltage is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_vsys_voltage(CHG_NUM, NULL));
}

/**
 * @brief Custom fake for get_vsys_voltage that can write to the output param
 */
static enum ec_error_list get_vsys_voltage_custom_fake(int chgnum, int port,
						       int *voltage)
{
	ARG_UNUSED(chgnum);
	ARG_UNUSED(port);

	*voltage = 2000;

	return EC_SUCCESS;
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_vsys_voltage)
{
	int vsys_voltage;

	fixture->mock_driver.get_vsys_voltage = get_vsys_voltage;
	get_vsys_voltage_fake.custom_fake = get_vsys_voltage_custom_fake;

	zassert_equal(EC_SUCCESS,
		      charger_get_vsys_voltage(CHG_NUM, &vsys_voltage));

	zassert_equal(1, get_vsys_voltage_fake.call_count);
	zassert_equal(CHG_NUM, get_vsys_voltage_fake.arg0_history[0]);
	zassert_equal(CHG_NUM, get_vsys_voltage_fake.arg1_history[0]);
	zassert_equal(2000, vsys_voltage);
}

ZTEST(common_charger_mocked_driver, test_charger_enable_bypass_mode__invalid)
{
	/* enable_bypass_mode is NULL */
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_enable_bypass_mode(CHG_NUM, false));
}

ZTEST_F(common_charger_mocked_driver, test_charger_enable_bypass_mode)
{
	fixture->mock_driver.enable_bypass_mode = enable_bypass_mode;
	enable_bypass_mode_fake.return_val = 123;

	zassert_equal(123, charger_enable_bypass_mode(CHG_NUM, true));

	zassert_equal(1, enable_bypass_mode_fake.call_count);
	zassert_true(enable_bypass_mode_fake.arg1_history[0]);
}

ZTEST(common_charger_mocked_driver, test_charger_get_params__error_flags)
{
	/* When one of the parameters cannot be retrieved, a corresponding flag
	 * is set. Since all of the driver functions are unimplemented by
	 * default, this should cause all error flags to be set.
	 */

	struct charger_params params;

	charger_get_params(&params);

	zassert_true(params.flags & CHG_FLAG_BAD_CURRENT);
	zassert_true(params.flags & CHG_FLAG_BAD_VOLTAGE);
	zassert_true(params.flags & CHG_FLAG_BAD_INPUT_CURRENT);
	zassert_true(params.flags & CHG_FLAG_BAD_STATUS);
	zassert_true(params.flags & CHG_FLAG_BAD_OPTION);
}

ZTEST(common_charger_mocked_driver,
      test_charger_get_input_current_limit__invalid)
{
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_input_current_limit(-1, false));
	zassert_equal(EC_ERROR_INVAL,
		      charger_get_input_current_limit(INT_MAX, false));
}

ZTEST(common_charger_mocked_driver,
      test_charger_get_input_current_limit__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_input_current_limit(CHG_NUM, false));
}

ZTEST(common_charger_mocked_driver, test_charger_get_input_current__invalid)
{
	zassert_equal(EC_ERROR_INVAL, charger_get_input_current(-1, NULL));
	zassert_equal(EC_ERROR_INVAL, charger_get_input_current(INT_MAX, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_input_current__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED,
		      charger_get_input_current(CHG_NUM, NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_manufacturer_id__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_manufacturer_id(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_manufacturer_id__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_manufacturer_id(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_device_id__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_device_id(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_device_id__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_device_id(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_option__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_get_option(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_get_option__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_get_option(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_set_option__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_set_option(0));
}

ZTEST(common_charger_mocked_driver, test_charger_set_option__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_set_option(0));
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_is_stable__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_false(chg_ramp_is_stable());
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_is_stable__unimpl)
{
	/* Returns 0 if ramp_is_stable not implemented */
	zassert_false(chg_ramp_is_stable());
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_is_detected__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_false(chg_ramp_is_detected());
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_is_detected__unimpl)
{
	/* Returns 0 if ramp_is_detected not implemented */
	zassert_false(chg_ramp_is_detected());
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_get_current_limit__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_false(chg_ramp_get_current_limit());
}

ZTEST(common_charger_mocked_driver, test_chg_ramp_get_current_limit__unimpl)
{
	/* Returns 0 if ramp_get_current_limit not implemented */
	zassert_false(chg_ramp_get_current_limit());
}

ZTEST(common_charger_mocked_driver, test_charger_post_init__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_post_init());
}

ZTEST(common_charger_mocked_driver, test_charger_post_init__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_post_init());
}

ZTEST(common_charger_mocked_driver, test_charger_get_info__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_is_null(charger_get_info());
}

ZTEST(common_charger_mocked_driver, test_charger_get_info__unimpl)
{
	zassert_is_null(charger_get_info());
}

ZTEST(common_charger_mocked_driver, test_charger_get_status__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_get_status(NULL));
}

ZTEST(common_charger_mocked_driver, test_charger_set_mode__invalid)
{
	uint8_t zero = 0;

	fake_charger_count = &zero;
	zassert_equal(EC_ERROR_INVAL, charger_set_mode(0));
}

ZTEST(common_charger_mocked_driver, test_charger_set_mode__unimpl)
{
	zassert_equal(EC_ERROR_UNIMPLEMENTED, charger_set_mode(0));
}

ZTEST_F(common_charger_mocked_driver, test_charger_set_vsys_compensation)
{
	fixture->mock_driver.set_vsys_compensation = set_vsys_compensation;
	set_vsys_compensation_fake.return_val = 123;

	zassert_equal(123, charger_set_vsys_compensation(CHG_NUM, NULL, 0, 0));

	zassert_equal(1, set_vsys_compensation_fake.call_count);
}

ZTEST_F(common_charger_mocked_driver, test_charger_is_icl_reached)
{
	fixture->mock_driver.is_icl_reached = is_icl_reached;
	is_icl_reached_fake.return_val = 123;

	zassert_equal(123, charger_is_icl_reached(CHG_NUM, NULL));

	zassert_equal(1, is_icl_reached_fake.call_count);
}

ZTEST_F(common_charger_mocked_driver, test_charger_enable_linear_charge)
{
	fixture->mock_driver.enable_linear_charge = enable_linear_charge;
	enable_linear_charge_fake.return_val = 123;

	zassert_equal(123, charger_enable_linear_charge(CHG_NUM, 0));

	zassert_equal(1, enable_linear_charge_fake.call_count);
}

ZTEST_F(common_charger_mocked_driver, test_charger_get_battery_cells)
{
	fixture->mock_driver.get_battery_cells = get_battery_cells;
	get_battery_cells_fake.return_val = 123;

	zassert_equal(123, charger_get_battery_cells(CHG_NUM, NULL));

	zassert_equal(1, get_battery_cells_fake.call_count);
}

static void *setup(void)
{
	static struct common_charger_mocked_driver_fixture f;

	zassert_true(board_get_charger_chip_count() > 0,
		     "Need at least one charger chip present.");

	/* Back up the current charger driver and substitute our own */
	f.saved_driver_ptr = chg_chips[CHG_NUM].drv;
	chg_chips[CHG_NUM].drv = &f.mock_driver;

	return &f;
}

static void reset(void *data)
{
	struct common_charger_mocked_driver_fixture *f = data;

	/* Reset the mock driver's function pointer table. Each tests adds these
	 * as-needed
	 */
	f->mock_driver = (struct charger_drv){ 0 };

	/* Reset fakes */
	RESET_FAKE(enable_otg_power);
	RESET_FAKE(set_otg_current_voltage);
	RESET_FAKE(is_sourcing_otg_power);
	RESET_FAKE(get_actual_current);
	RESET_FAKE(get_actual_voltage);
	RESET_FAKE(set_voltage);
	RESET_FAKE(get_vsys_voltage);
	RESET_FAKE(enable_bypass_mode);
	RESET_FAKE(set_vsys_compensation);
	RESET_FAKE(is_icl_reached);
	RESET_FAKE(enable_linear_charge);
	RESET_FAKE(get_battery_cells);

	fake_charger_count = NULL;
}

static void teardown(void *data)
{
	struct common_charger_mocked_driver_fixture *f = data;

	/* Restore the original driver */
	chg_chips[CHG_NUM].drv = f->saved_driver_ptr;
}

ZTEST_SUITE(common_charger_mocked_driver, drivers_predicate_post_main, setup,
	    reset, reset, teardown);
