/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for power signals API
 */

#include <device.h>

#include <drivers/espi.h>
#include <drivers/espi_emul.h>
#include <drivers/gpio/gpio_emul.h>
#include <logging/log.h>
#include <zephyr.h>
#include <ztest.h>

#include "power_signals.h"

#include "ec_tasks.h"
#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "test_state.h"

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
{	PWR_EN_PP5000_A, 10},
{	PWR_EN_PP3300_A, 11},
{	PWR_RSMRST, 12},
{	PWR_EC_PCH_RSMRST, 13},
{	PWR_SLP_S0, 14},
{	PWR_SLP_S3, 15},
{	PWR_SLP_SUS, 16},
{	PWR_EC_SOC_DSW_PWROK, 17},
{	PWR_VCCST_PWRGD, 18},
{	PWR_IMVP9_VRRDY, 19},
{	PWR_PCH_PWROK, 20},
{	PWR_EC_PCH_SYS_PWROK, 21},
{	PWR_SYS_RST, 22},
};

/*
 * Retrieve the pin number corresponding to this signal.
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
		      "set succeeded");
	/* Can't enable interrupt on output */
	zassert_equal(-EINVAL, power_signal_enable_interrupt(PWR_VCCST_PWRGD),
		      "enable interrupt succeeded");
	/* Can't disable interrupt on output */
	zassert_equal(-EINVAL, power_signal_disable_interrupt(PWR_VCCST_PWRGD),
		      "disable interrupt succeeded");
	/* Can't enable interrupt on input with no interrupt flags */
	zassert_equal(-EINVAL, power_signal_enable_interrupt(PWR_IMVP9_VRRDY),
		      "enable interrupt succeeded");
	/* Can't disable interrupt on input with no interrupt flags */
	zassert_equal(-EINVAL,
		      power_signal_disable_interrupt(PWR_IMVP9_VRRDY),
		      "enable interrupt succeeded");
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
	zassert_equal(0, power_signal_set(PWR_ALL_SYS_PWRGD, 1),
		      "set failed");
	zassert_equal(1, power_signal_get(PWR_ALL_SYS_PWRGD),
		      "get failed");
}

/**
 * @brief TestPurpose: Verify output signals are de-asserted at startup.
 *
 * @details
 * Confirm that output signals are initialised correctly.
 *
 * Expected Results
 *  - Output signals are initialised as de-asserted.
 */
ZTEST(signals, test_init_outputs)
{
	zassert_equal(0, emul_get(PWR_EN_PP5000_A),
		      "PWR_EN_PP5000_A init failed");
	zassert_equal(0, emul_get(PWR_EN_PP3300_A),
		      "PWR_EN_PP3300_A init failed");
	zassert_equal(0, emul_get(PWR_EC_PCH_RSMRST),
		      "PWR_EC_PCH_RSMRST init failed");
	zassert_equal(0, emul_get(PWR_EC_SOC_DSW_PWROK),
		      "PWR_EC_SOC_DSW_PWROK init failed");
	zassert_equal(0, emul_get(PWR_PCH_PWROK),
		      "PWR_PCH_PWROK init failed");
	zassert_equal(1, emul_get(PWR_SYS_RST),
		      "PWR_SYS_RST init failed");
}

/**
 * @brief TestPurpose: Verify input signals are read correctly
 *
 * @details
 * Confirm that input signals are read correctly.
 *
 * Expected Results
 *  - Input signals are read correctly.
 */
ZTEST(signals, test_gpio_input)
{
	emul_set(PWR_RSMRST, 1);
	zassert_equal(1, power_signal_get(PWR_RSMRST),
		      "PWR_RSMRST expected 1");
	emul_set(PWR_RSMRST, 0);
	zassert_equal(0, power_signal_get(PWR_RSMRST),
		      "PWR_RSMRST expected 0");
	/* ACTIVE_LOW input */
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(1, power_signal_get(PWR_SLP_S0),
		      "PWR_SLP_S0 expected 1");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(0, power_signal_get(PWR_SLP_S0),
		      "PWR_SLP_S0 expected 0");
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
		      "PWR_PCH_PWROK expected 1");
	power_signal_set(PWR_PCH_PWROK, 0);
	zassert_equal(0, emul_get(PWR_PCH_PWROK),
		      "PWR_PCH_PWROK expected 0");
	/* ACTIVE_LOW output */
	power_signal_set(PWR_SYS_RST, 0);
	zassert_equal(1, emul_get(PWR_SYS_RST),
		      "PWR_SYS_RST expected 1");
	power_signal_set(PWR_SYS_RST, 1);
	zassert_equal(0, emul_get(PWR_SYS_RST),
		      "PWR_SYS_RST expected 0");
}

/**
 * @brief TestPurpose: Verify signal mask handling
 *
 * @details
 * Confirm that signal mask processing works.
 *
 * Expected Results
 *  - Multiple signal mask processing works
 */
ZTEST(signals, test_signal_mask)
{
	power_signal_mask_t vm = POWER_SIGNAL_MASK(PWR_IMVP9_VRRDY);
	power_signal_mask_t m;

	/* Use non-interrupt GPIO */
	emul_set(PWR_IMVP9_VRRDY, 0);
	m = power_get_signals() & vm;
	zassert_equal(0, (power_get_signals() & vm), "Expected 0 signals");
	emul_set(PWR_IMVP9_VRRDY, 1);
	zassert_equal(0, (power_get_signals() & vm), "Expected 0 signals");
	power_update_signals();
	zassert_equal(vm, (power_get_signals() & vm),
		"Expected non-zero signals");
	zassert_equal(true, power_signals_match(vm, vm),
		"Expected signal match");
	zassert_equal(-ETIMEDOUT, power_wait_mask_signals_timeout(vm, 0, 5),
		"Expected timeout");
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
	power_signal_mask_t dm = 0xDEADBEEF;

	power_set_debug(dm);
	zassert_equal(dm, power_get_debug(), "Debug mask does not match");
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
	/* Check that changes update the signal mask. */
	power_signal_mask_t rsm = POWER_SIGNAL_MASK(PWR_RSMRST);
	power_signal_mask_t s3 = POWER_SIGNAL_MASK(PWR_SLP_S3);
	power_signal_mask_t s0 = POWER_SIGNAL_MASK(PWR_SLP_S0);

	emul_set(PWR_RSMRST, 1);
	zassert_equal(true, power_signals_on(rsm), "PWR_RSMRST not updated");
	emul_set(PWR_RSMRST, 0);
	zassert_equal(true, power_signals_off(rsm), "PWR_RSMRST not updated");

	/* ACTIVE_LOW */
	emul_set(PWR_SLP_S3, 0);
	zassert_equal(true, power_signals_on(s3), "SLP_S3 not updated");
	emul_set(PWR_SLP_S3, 1);
	zassert_equal(true, power_signals_off(s3), "SLP_S3 not updated");

	/* Check that disabled interrupt does not trigger */
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(false, power_signals_on(s0), "SLP_S0 updated");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(false, power_signals_on(s0), "SLP_S0 updated");

	power_signal_enable_interrupt(PWR_SLP_S0);
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(true, power_signals_on(s0), "SLP_S0 not updated");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(true, power_signals_off(s0), "SLP_S0 not updated");

	power_signal_disable_interrupt(PWR_SLP_S0);
	emul_set(PWR_SLP_S0, 0);
	zassert_equal(false, power_signals_on(s0), "SLP_S0 updated");
	emul_set(PWR_SLP_S0, 1);
	zassert_equal(true, power_signals_off(s0), "SLP_S0 updated");
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
	/* Signal is inverted */
	emul_espi_host_send_vw(espi, ESPI_VWIRE_SIGNAL_SLP_S5, 0);
	zassert_equal(1, power_signal_get(PWR_SLP_S5),
		"VW SLP_S5 should be 1");
	emul_espi_host_send_vw(espi, ESPI_VWIRE_SIGNAL_SLP_S5, 1);
	zassert_equal(0, power_signal_get(PWR_SLP_S5),
		"VW SLP_S5 should be 0");
}

static void *init_dev(void)
{
	emul_port = device_get_binding("GPIO_0");

	return NULL;
}

static void init_signals(void *data)
{
	power_signal_init();
}

/**
 * @brief Test Suite: Verifies power signal functionality.
 */
ZTEST_SUITE(signals, ap_power_predicate_post_main,
	    init_dev, init_signals, NULL, NULL);
