/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for power signals API
 */

#include "ec_tasks.h"
#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "power_signals.h"
#include "test_state.h"
#include "vcmp_mock.h"

#include <zephyr/device.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

extern bool gpio_test_interrupt_triggered;

static const struct device *emul_port;

/*
 * Map of signal to GPIO pin.
 * This must match overlay.dts
 */
static struct {
	enum power_signal signal;
	int pin;
} signal_to_pin_table[] = {
	{ PWR_EN_PP5000_A, 10 }, { PWR_EN_PP3300_A, 11 },
	{ PWR_RSMRST, 12 },	 { PWR_EC_PCH_RSMRST, 13 },
	{ PWR_SLP_S0, 14 },	 { PWR_SLP_S3, 15 },
	{ PWR_SLP_SUS, 16 },	 { PWR_EC_SOC_DSW_PWROK, 17 },
	{ PWR_VCCST_PWRGD, 18 }, { PWR_IMVP9_VRRDY, 19 },
	{ PWR_PCH_PWROK, 20 },	 { PWR_EC_PCH_SYS_PWROK, 21 },
	{ PWR_SYS_RST, 22 },
};

/*
 * Retrieve the GPIO pin number corresponding to this signal.
 */
static int signal_to_pin(enum power_signal signal)
{
	for (int i = 0; i < ARRAY_SIZE(signal_to_pin_table); i++) {
		if (signal_to_pin_table[i].signal == signal) {
			return signal_to_pin_table[i].pin;
		}
	}
	zassert_unreachable("Unknown signal");
	return -1;
}

/*
 * Set the raw input of the corresponding GPIO
 */
static void emul_set(enum power_signal signal, int value)
{
	gpio_emul_input_set(emul_port, signal_to_pin(signal), value);
}

/*
 * Get the raw output of the corresponding GPIO
 */
static int emul_get(enum power_signal signal)
{
	return gpio_emul_output_get(emul_port, signal_to_pin(signal));
}

/**
 * @brief TestPurpose: Check input/output validation
 *
 * @details
 * Validate that input/output GPIOs do not accept invalidate requests.
 *
 * Expected Results
 *  - Error when invalid requests are made.
 */
ZTEST(signals, test_validate_request)
{
	/* Can't set output on input */
	zassert_equal(-EINVAL, power_signal_set(PWR_SLP_S0, 1),
		      "power_signal_set on input pin should not succeed");
	/* Can't enable interrupt on output */
	zassert_equal(-EINVAL, power_signal_enable(PWR_VCCST_PWRGD),
		      "enable interrupt on output pin should not succeed");
	/* Can't disable interrupt on output */
	zassert_equal(-EINVAL, power_signal_disable(PWR_VCCST_PWRGD),
		      "disable interrupt on output pin should not succeed");
	/* Can't enable interrupt on input with no interrupt flags */
	zassert_equal(-EINVAL, power_signal_enable(PWR_IMVP9_VRRDY),
		      "enable interrupt on input pin without interrupt config");
	/* Can't disable interrupt on input with no interrupt flags */
	zassert_equal(
		-EINVAL, power_signal_disable(PWR_IMVP9_VRRDY),
		"disable interrupt on input pin without interrupt config");
	/* Invalid signal - should be rejectde */
	zassert_equal(-EINVAL, power_signal_get(-1),
		      "power_signal_get with -1 signal should fail");
	zassert_equal(-EINVAL, power_signal_set(-1, 1),
		      "power_signal_set with -1 signal should fail");
	zassert_equal(-EINVAL, power_signal_enable(-1),
		      "enable interrupt with -1 signal should fail");
	zassert_equal(-EINVAL, power_signal_disable(-1),
		      "disable interrupt with -1 signal should fail");
}

/**
 * @brief TestPurpose: Verify board specific signals
 *
 * @details
 * Validate access to board specific signals
 *
 * Expected Results
 *  - Can set and get board specific signals.
 */
ZTEST(signals, test_board_signals)
{
	/*
	 * Check that the board level signals get correctly invoked.
	 */
	zassert_ok(power_signal_set(PWR_ALL_SYS_PWRGD, 1),
		   "power_signal_set on board signal failed");
	zassert_equal(1, power_signal_get(PWR_ALL_SYS_PWRGD),
		      "power_signal_get on board signal should return 1");
}

/**
 * @brief TestPurpose: Check name retrieval
 *
 * @details
 * Validate out of bounds request
 *
 * Expected Results
 *  - Retrieve name or null for request.
 */
ZTEST(signals, test_signal_name)
{
	for (int signal = 0; signal < POWER_SIGNAL_COUNT; signal++) {
		zassert_not_null(power_signal_name(signal),
				 "Signal name for %d should be not null",
				 signal);
	}
	zassert_is_null(power_signal_name(-1),
			"Out of bounds signal name should be null");
	zassert_is_null(power_signal_name(POWER_SIGNAL_COUNT),
			"Out of bounds signal name should be null");
}

/**
 * @brief TestPurpose: Verify output signals are de-asserted at startup.
 *
 * @details
 * Confirm that output signals are initialised correctly.
 * Output pins are by default set to a de-asserted state at start-up, so
 * signals that are active-low should be set to physical high, signals
 * that are active-high should be set to physical low.
 * In overlay.dts, the only output power signal that is active-low is
 * PWR_SYS_RST
 *
 * Expected Results
 *  - Output signals are initialised as de-asserted.
 */
ZTEST(signals, test_init_outputs)
{
	static const enum power_signal active_high[] = {
		PWR_EN_PP5000_A, PWR_EN_PP3300_A, PWR_EC_PCH_RSMRST,
		PWR_EC_SOC_DSW_PWROK, PWR_PCH_PWROK
	};
	static const enum power_signal active_low[] = { PWR_SYS_RST };

	for (int i = 0; i < ARRAY_SIZE(active_high); i++) {
		zassert_equal(0, emul_get(active_high[i]),
			      "Signal %d (%s) init to de-asserted state failed",
			      active_high[i],
			      power_signal_name(active_high[i]));
	}
	for (int i = 0; i < ARRAY_SIZE(active_low); i++) {
		zassert_equal(1, emul_get(active_low[i]),
			      "Signal %d (%s) init to de-asserted state failed",
			      active_low[i], power_signal_name(active_low[i]));
	}
}

/**
 * @brief TestPurpose: Verify input signals are read correctly
 *
 * @details
 * Confirm that input signals are read correctly. Signal values
 * are set via the GPIO emul driver.
 *
 * Expected Results
 *  - Input signals are read correctly.
 */
ZTEST(signals, test_gpio_input)
{
	emul_set(PWR_RSMRST, 1);
	zassert_equal(1, power_signal_get(PWR_RSMRST),
		      "power_signal_get of PWR_RSMRST should be 1");
	emul_set(PWR_RSMRST, 0);
	zassert_equal(0, power_signal_get(PWR_RSMRST),
		      "power_signal_get of PWR_RSMRST should be 0");
	/* ACTIVE_LOW input */
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(
		1, power_signal_get(PWR_SLP_S0),
		"power_signal_get of active-low signal PWR_SLP_S0 should be 1");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(0, power_signal_get(PWR_SLP_S0),
		      "power_signal_get of active-low PWR_SLP_S0 should be 0");
}

/**
 * @brief TestPurpose: Verify output signals are written correctly
 *
 * @details
 * Confirm that output signals are written correctly.
 *
 * Expected Results
 *  - Set signals are written correctly
 */
ZTEST(signals, test_gpio_output)
{
	power_signal_set(PWR_PCH_PWROK, 1);
	zassert_equal(1, emul_get(PWR_PCH_PWROK),
		      "power_signal_set of PWR_PCH_PWROK should be 1");
	power_signal_set(PWR_PCH_PWROK, 0);
	zassert_equal(0, emul_get(PWR_PCH_PWROK),
		      "power_signal_set of PWR_PCH_PWROK should be 0");
	/* ACTIVE_LOW output */
	power_signal_set(PWR_SYS_RST, 0);
	zassert_equal(1, emul_get(PWR_SYS_RST),
		      "power_signal_set of PWR_SYS_RST should be 1");
	power_signal_set(PWR_SYS_RST, 1);
	zassert_equal(0, emul_get(PWR_SYS_RST),
		      "power_signal_set of PWR_SYS_RST should be 0");
}

/**
 * @brief TestPurpose: Verify signal mask handling
 *
 * @details
 * Confirm that signal mask processing works as expected,
 * such as checking that pin changes send interrupts that
 * modifies the power signal mask.
 *
 * Expected Results
 *  - Multiple signal mask processing works
 */
ZTEST(signals, test_signal_mask)
{
	power_signal_mask_t vm = POWER_SIGNAL_MASK(PWR_IMVP9_VRRDY);
	power_signal_mask_t bm = POWER_SIGNAL_MASK(PWR_ALL_SYS_PWRGD);
	power_signal_mask_t m;

	/*
	 * Set board level (polled) signal.
	 */
	power_signal_set(PWR_ALL_SYS_PWRGD, 1);
	zassert_equal(
		bm, (power_get_signals() & bm),
		"Expected PWR_ALL_SYS_PWRGD signal to be present in mask");
	/*
	 * Use GPIO that does not interrupt to confirm that a pin change
	 * will not update the power signal mask.
	 */
	emul_set(PWR_IMVP9_VRRDY, 0);
	m = power_get_signals() & vm;
	zassert_equal(0, (power_get_signals() & vm), "Expected mask to be 0");
	emul_set(PWR_IMVP9_VRRDY, 1);
	zassert_equal(0, (power_get_signals() & vm), "Expected mask to be 0");
	zassert_equal(true, power_signals_match(bm, bm),
		      "Expected match of mask to signal match");
	zassert_equal(-ETIMEDOUT, power_wait_mask_signals_timeout(bm, 0, 5),
		      "Expected timeout waiting for mask to be 0");
	zassert_ok(power_wait_mask_signals_timeout(0, vm, 5),
		   "expected match with a 0 mask (always true)");
}

/**
 * @brief TestPurpose: Verify set/get of debug mask
 *
 * @details
 * Confirm that debug mask setting works
 *
 * Expected Results
 *  - Can set/get debug mask
 */
ZTEST(signals, test_debug_mask)
{
	power_signal_mask_t old;
	power_signal_mask_t dm = 0xDEADBEEF;

	old = power_get_debug();
	power_set_debug(dm);
	zassert_equal(dm, power_get_debug(),
		      "Debug mask does not match set value");
	/*
	 * Reset back to default.
	 */
	power_set_debug(old);
}

/**
 * @brief TestPurpose: Verify interrupts work as expected
 *
 * @details
 * For no-enable interrupts, ensure that they are not enabled at the start.
 * For default, ensure that the interrupts are enabled.
 * Check that enable/disable interrupt works.
 *
 * Expected Results
 *  - interrupt enabling/disabling works
 */
ZTEST(signals, test_gpio_interrupts)
{
	power_signal_mask_t rsm = POWER_SIGNAL_MASK(PWR_RSMRST);
	power_signal_mask_t s3 = POWER_SIGNAL_MASK(PWR_SLP_S3);
	power_signal_mask_t s0 = POWER_SIGNAL_MASK(PWR_SLP_S0);

	/* Check that GPIO pin changes update the signal mask. */
	emul_set(PWR_RSMRST, 1);
	zassert_equal(true, power_signals_on(rsm),
		      "PWR_RSMRST not updated in mask");
	emul_set(PWR_RSMRST, 0);
	zassert_equal(true, power_signals_off(rsm),
		      "PWR_RSMRST not updated in mask");

	/*
	 * Check that an ACTIVE_LOW signal gets asserted in
	 * the mask (physical value of GPIO pin 0 is set as 1 in mask)
	 */
	emul_set(PWR_SLP_S3, 0);
	zassert_equal(true, power_signals_on(s3),
		      "SLP_S3 signal should be on in mask");
	emul_set(PWR_SLP_S3, 1);
	zassert_equal(true, power_signals_off(s3),
		      "SLP_S3 should be off in mask");

	/*
	 * Check that disabled interrupt on the GPIO does not trigger
	 * and update the mask.
	 */
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(false, power_signals_on(s0),
		      "SLP_S0 should not have updated");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(false, power_signals_on(s0),
		      "SLP_S0 should not have updated");

	power_signal_enable(PWR_SLP_S0);
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(true, power_signals_on(s0),
		      "SLP_S0 should have updated the mask");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(true, power_signals_off(s0),
		      "SLP_S0 should have updated the mask");

	/*
	 * Disable the GPIO interrupt again.
	 */
	power_signal_disable(PWR_SLP_S0);
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(false, power_signals_on(s0),
		      "SLP_S0 should not have updated the mask");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(true, power_signals_off(s0),
		      "SLP_S0 should not have updated the mask");
}

/**
 * @brief TestPurpose: Verify reception of VW signals
 *
 * @details
 * Confirm that ESPI virtual wire signals can be received.
 *
 * Expected Results
 *  - Virtual wire signals are received.
 */
ZTEST(signals, test_espi_vw)
{
	const struct device *espi =
		DEVICE_DT_GET_ANY(zephyr_espi_emul_controller);

	zassert_not_null(espi, "Cannot get ESPI device");
	/*
	 * Send a VW signal, and check that it is received.
	 * The signal is configured as an inverted signal,
	 * so sending a 0 value should be received as a signal.
	 */
	emul_espi_host_send_vw(espi, ESPI_VWIRE_SIGNAL_SLP_S5, 0);
	zassert_equal(1, power_signal_get(PWR_SLP_S5), "VW SLP_S5 should be 1");
	emul_espi_host_send_vw(espi, ESPI_VWIRE_SIGNAL_SLP_S5, 1);
	zassert_equal(0, power_signal_get(PWR_SLP_S5), "VW SLP_S5 should be 0");
}

enum trigger_expect {
	ADC_TRIG_NONE,
	ADC_TRIG_HIGH,
	ADC_TRIG_LOW,
};

static void check_adc_triggers(enum trigger_expect expect)
{
	const struct device *trigger_high =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_high));
	const struct device *trigger_low =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_low));
	struct sensor_value val_high;
	struct sensor_value val_low;

	sensor_attr_get(trigger_high, SENSOR_CHAN_VOLTAGE, SENSOR_ATTR_ALERT,
			&val_high);
	sensor_attr_get(trigger_low, SENSOR_CHAN_VOLTAGE, SENSOR_ATTR_ALERT,
			&val_low);
	switch (expect) {
	case ADC_TRIG_NONE:
		zassert_equal(0, val_high.val1,
			      "high trigger should be disabled");
		zassert_equal(0, val_low.val1,
			      "low trigger should be disabled");
		break;
	case ADC_TRIG_HIGH:
		zassert_equal(1, val_high.val1,
			      "high trigger should be enabled");
		zassert_equal(0, val_low.val1,
			      "low trigger should be disabled");
		break;
	case ADC_TRIG_LOW:
		zassert_equal(0, val_high.val1,
			      "high trigger should be disabled");
		zassert_equal(1, val_low.val1, "low trigger should be enabled");
		break;
	}
}

ZTEST(signals, test_adc_get)
{
	const struct device *trigger_high =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_high));
	const struct device *trigger_low =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_low));

	/* Always start low */
	vcmp_mock_trigger(trigger_low);

	zassert_equal(0, power_signal_get(PWR_PG_PP1P05),
		      "power_signal_get of PWR_PG_PP1P05 should be 0");
	check_adc_triggers(ADC_TRIG_HIGH);

	/* Signal goes up... */
	vcmp_mock_trigger(trigger_high);

	zassert_equal(1, power_signal_get(PWR_PG_PP1P05),
		      "power_signal_get of PWR_PG_PP1P05 should be 1");
	check_adc_triggers(ADC_TRIG_LOW);

	/* ...signal goes down. */
	vcmp_mock_trigger(trigger_low);

	zassert_equal(0, power_signal_get(PWR_PG_PP1P05),
		      "power_signal_get of PWR_PG_PP1P05 should be 0");
	check_adc_triggers(ADC_TRIG_HIGH);
}

ZTEST(signals, test_adc_enable_disable)
{
	const struct device *trigger_high =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_high));
	const struct device *trigger_low =
		DEVICE_DT_GET(DT_NODELABEL(mock_cmp_low));

	/* Always start from low */
	vcmp_mock_trigger(trigger_low);
	check_adc_triggers(ADC_TRIG_HIGH);

	power_signal_disable(PWR_PG_PP1P05);
	check_adc_triggers(ADC_TRIG_NONE);

	power_signal_enable(PWR_PG_PP1P05);
	check_adc_triggers(ADC_TRIG_HIGH);

	vcmp_mock_trigger(trigger_high);
	check_adc_triggers(ADC_TRIG_LOW);

	power_signal_disable(PWR_PG_PP1P05);
	check_adc_triggers(ADC_TRIG_NONE);

	power_signal_enable(PWR_PG_PP1P05);
	check_adc_triggers(ADC_TRIG_LOW);
}

static void *init_dev(void)
{
	emul_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));

	return NULL;
}

static void init_signals(void *data)
{
	power_signal_init();
}

/**
 * @brief Test Suite: Verifies power signal functionality.
 */
ZTEST_SUITE(signals, ap_power_predicate_post_main, init_dev, init_signals, NULL,
	    NULL);
