/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/zephyr.h>
#include <ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include "common.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "fff.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "test/drivers/stubs.h"
#include "task.h"
#include "tcpm/ps8xxx_public.h"
#include "tcpm/tcpci.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"

#include "usb_mux.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/** Copy of original usb_muxes[USB_PORT_C1] */
struct usb_mux usb_mux_c1;

/** Number of usb mux proxies in chain */
#define NUM_OF_PROXY	3

/** Pointers to original usb muxes chain of port c1 */
const struct usb_mux *org_mux[NUM_OF_PROXY];

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VALUE_FUNC1(int, proxy_init, const struct usb_mux *);
static int proxy_init_custom(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;
	int ec = EC_SUCCESS;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->init != NULL) {
		ec = org_mux[i]->driver->init(org_mux[i]);
	}

	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(proxy_init);
	}

	/* Discard this call if made from different thread */
	proxy_init_fake.call_count--;

	return ec;
}

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VALUE_FUNC3(int, proxy_set, const struct usb_mux *, mux_state_t, bool *);
static int proxy_set_custom(const struct usb_mux *me, mux_state_t mux_state,
			    bool *ack_required)
{
	int i = me->i2c_addr_flags;
	int ec = EC_SUCCESS;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->set != NULL) {
		ec = org_mux[i]->driver->set(org_mux[i], mux_state,
					     ack_required);
	}

	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(proxy_set);
	}

	/* Discard this call if made from different thread */
	proxy_set_fake.call_count--;

	return ec;
}

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VALUE_FUNC2(int, proxy_get, const struct usb_mux *, mux_state_t *);
/** Sequence of mux_state values returned by proxy_get function */
static mux_state_t proxy_get_mux_state_seq[NUM_OF_PROXY];
/** Index of next mux_state to return from proxy_get_function */
static int proxy_get_mux_state_seq_idx;
/** Set all mux_state in sequence to the same state value */
static void set_proxy_get_mux_state_seq(mux_state_t state)
{
	proxy_get_mux_state_seq_idx = 0;
	for (int i = 0; i < NUM_OF_PROXY; i++) {
		proxy_get_mux_state_seq[i] = state;
	}
}

static int proxy_get_custom(const struct usb_mux *me, mux_state_t *mux_state)
{
	int i = me->i2c_addr_flags;
	int ec = EC_SUCCESS;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->get != NULL) {
		ec = org_mux[i]->driver->get(org_mux[i], mux_state);
	}

	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		zassert_true(proxy_get_mux_state_seq_idx < NUM_OF_PROXY,
			     "%s called too many times without resetting "
			     "mux_state_seq", __func__);
		*mux_state =
			proxy_get_mux_state_seq[proxy_get_mux_state_seq_idx];
		proxy_get_mux_state_seq_idx++;
		RETURN_FAKE_RESULT(proxy_get);
	}

	/* Discard this call if made from different thread */
	proxy_get_fake.call_count--;

	return ec;
}

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VALUE_FUNC1(int, proxy_enter_low_power_mode, const struct usb_mux *);
static int proxy_enter_low_power_mode_custom(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;
	int ec = EC_SUCCESS;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->enter_low_power_mode != NULL) {
		ec = org_mux[i]->driver->enter_low_power_mode(org_mux[i]);
	}

	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(proxy_enter_low_power_mode);
	}

	/* Discard this call if made from different thread */
	proxy_enter_low_power_mode_fake.call_count--;

	return ec;
}

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VALUE_FUNC1(int, proxy_chipset_reset, const struct usb_mux *);
static int proxy_chipset_reset_custom(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;
	int ec = EC_SUCCESS;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->chipset_reset != NULL) {
		ec = org_mux[i]->driver->chipset_reset(org_mux[i]);
	}

	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(proxy_chipset_reset);
	}

	/* Discard this call if made from different thread */
	proxy_chipset_reset_fake.call_count--;

	return ec;
}

/** Proxy function for fw update capability */
static bool proxy_fw_update_cap(void)
{
	return true;
}

/** Proxy function which check calls from usb_mux framework to driver */
FAKE_VOID_FUNC3(proxy_hpd_update, const struct usb_mux *, mux_state_t, bool *);
static void proxy_hpd_update_custom(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required)
{
	int i = me->i2c_addr_flags;

	zassert_true(i < NUM_OF_PROXY, "Proxy called for non proxy usb_mux");

	if (org_mux[i] != NULL &&
	    org_mux[i]->hpd_update != NULL) {
		org_mux[i]->hpd_update(org_mux[i], mux_state, ack_required);
	}

	if (task_get_current() != TASK_ID_TEST_RUNNER) {
		/* Discard this call if made from different thread */
		proxy_hpd_update_fake.call_count--;
	}
}

/** Usb mux driver with proxy functions */
const struct usb_mux_driver proxy_usb_mux = {
	.init = &proxy_init,
	.set = &proxy_set,
	.get = &proxy_get,
	.enter_low_power_mode = &proxy_enter_low_power_mode,
	.chipset_reset = &proxy_chipset_reset,
	.is_retimer_fw_update_capable = &proxy_fw_update_cap,
};

/** Mock function used in init test */
FAKE_VALUE_FUNC1(int, mock_board_init, const struct usb_mux *);
static int mock_board_init_custom(const struct usb_mux *me)
{
	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(mock_board_init);
	}

	/* Discard this call if made from different thread */
	mock_board_init_fake.call_count--;

	return EC_SUCCESS;
}

/** Mock function used in set test */
FAKE_VALUE_FUNC2(int, mock_board_set, const struct usb_mux *, mux_state_t);
static int mock_board_set_custom(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	if (task_get_current() == TASK_ID_TEST_RUNNER) {
		RETURN_FAKE_RESULT(mock_board_set);
	}

	/* Discard this call if made from different thread */
	mock_board_set_fake.call_count--;

	return EC_SUCCESS;
}

/**
 * Reset state of all fake functions, setup custom fake functions and set
 * default return value to EC_SUCCESS (all functions which has return value)
 */
static void reset_proxy_fakes(void)
{
	RESET_FAKE(proxy_init);
	RESET_FAKE(proxy_set);
	RESET_FAKE(proxy_get);
	RESET_FAKE(proxy_enter_low_power_mode);
	RESET_FAKE(proxy_chipset_reset);
	RESET_FAKE(proxy_hpd_update);
	RESET_FAKE(mock_board_init);
	RESET_FAKE(mock_board_set);

	/* Setup proxy functions */
	proxy_init_fake.custom_fake = proxy_init_custom;
	proxy_set_fake.custom_fake = proxy_set_custom;
	proxy_get_fake.custom_fake = proxy_get_custom;
	proxy_enter_low_power_mode_fake.custom_fake =
					proxy_enter_low_power_mode_custom;
	proxy_chipset_reset_fake.custom_fake = proxy_chipset_reset_custom;
	proxy_hpd_update_fake.custom_fake = proxy_hpd_update_custom;
	mock_board_init_fake.custom_fake = mock_board_init_custom;
	mock_board_set_fake.custom_fake = mock_board_set_custom;

	/* Set default return value */
	proxy_init_fake.return_val = EC_SUCCESS;
	proxy_set_fake.return_val = EC_SUCCESS;
	proxy_get_fake.return_val = EC_SUCCESS;
	proxy_enter_low_power_mode_fake.return_val = EC_SUCCESS;
	proxy_chipset_reset_fake.return_val = EC_SUCCESS;
	mock_board_init_fake.return_val = EC_SUCCESS;
	mock_board_set_fake.return_val = EC_SUCCESS;
}

/** Chain of 3 proxy usb muxes */
struct usb_mux proxy_chain_2 = {
	.usb_port = USBC_PORT_C1,
	.driver = &proxy_usb_mux,
	.next_mux = NULL,
	.i2c_addr_flags = 2,
	.hpd_update = &proxy_hpd_update,
};

struct usb_mux proxy_chain_1 = {
	.usb_port = USBC_PORT_C1,
	.driver = &proxy_usb_mux,
	.next_mux = &proxy_chain_2,
	.i2c_addr_flags = 1,
	.hpd_update = &proxy_hpd_update,
};

struct usb_mux proxy_chain_0 = {
	.usb_port = USBC_PORT_C1,
	.driver = &proxy_usb_mux,
	.next_mux = &proxy_chain_1,
	.i2c_addr_flags = 0,
	.hpd_update = &proxy_hpd_update,
};


/** Setup first 3 usb muxes of port 1 with proxy */
static void setup_usb_mux_proxy_chain(void)
{
	const struct usb_mux *t;
	int i;

	memcpy(&usb_mux_c1, &usb_muxes[USBC_PORT_C1], sizeof(struct usb_mux));
	memcpy(&usb_muxes[USBC_PORT_C1], &proxy_chain_0,
	       sizeof(struct usb_mux));

	/*
	 * Setup org_mux array to point real driver which should be called by
	 * each proxy
	 */
	t = &usb_mux_c1;
	for (i = 0; i < NUM_OF_PROXY; i++) {
		org_mux[i] = t;
		if (t != NULL) {
			t = t->next_mux;
		}
	}

	if (org_mux[2] != NULL) {
		proxy_chain_2.next_mux = org_mux[2]->next_mux;
	} else {
		proxy_chain_2.next_mux = NULL;
	}
}

/** Restore original usb_mux chain without proxy */
static void restore_usb_mux_chain(void)
{
	memcpy(&usb_muxes[USBC_PORT_C1], &usb_mux_c1, sizeof(struct usb_mux));
}

/**
 * Check if given proxy function was called num times and if first argument was
 * pointer to the right proxy chain element. First argument is
 * const struct usb_mux * for all struct usb_mux_driver callbacks.
 */
#define CHECK_PROXY_FAKE_CALL_CNT(proxy, num)				\
	do {								\
		zassert_equal(num, proxy##_fake.call_count, "%d != %d",	\
			      num, proxy##_fake.call_count);		\
		if (num >= 1) {						\
			zassert_equal(&usb_muxes[USBC_PORT_C1],		\
				      proxy##_fake.arg0_history[0],	\
				      NULL);				\
		}							\
		if (num >= 2) {						\
			zassert_equal(&proxy_chain_1,			\
				      proxy##_fake.arg0_history[1],	\
				      NULL);				\
		}							\
		if (num >= 3) {						\
			zassert_equal(&proxy_chain_2,			\
				      proxy##_fake.arg0_history[2],	\
				      NULL);				\
		}							\
	} while (0)

/**
 * Do the same thing as CHECK_PROXY_FAKE_CALL_CNT and check if second argument
 * was the same as given state. hpd_update and set callback have mux_state_t
 * as second argument.
 */
#define CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy, num, state)		\
	do {								\
		CHECK_PROXY_FAKE_CALL_CNT(proxy, num);			\
		if (num >= 1) {						\
			zassert_equal(state,				\
				      proxy##_fake.arg1_history[0],	\
				      "0x%x != 0x%x", state,		\
				      proxy##_fake.arg1_history[0]);	\
		}							\
		if (num >= 2) {						\
			zassert_equal(state,				\
				      proxy##_fake.arg1_history[1],	\
				      "0x%x != 0x%x", state,		\
				      proxy##_fake.arg1_history[1]);	\
		}							\
		if (num >= 3) {						\
			zassert_equal(state,				\
				      proxy##_fake.arg1_history[2],	\
				      "0x%x != 0x%x", state,		\
				      proxy##_fake.arg1_history[2]);	\
		}							\
	} while (0)

/** Test usb_mux init */
ZTEST(usb_uninit_mux, test_usb_mux_init)
{
	int fail_on_2nd_ret[] = {EC_SUCCESS, EC_ERROR_NOT_POWERED};

	/* Set AP to normal state to init BB retimer */
	test_set_chipset_to_s0();

	/* Test successful initialisation */
	usb_mux_init(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);

	/*
	 * Test failed initialisation. Muxes that are in chain after
	 * the one which fails shouldn't be called
	 */
	reset_proxy_fakes();
	SET_RETURN_SEQ(proxy_init, fail_on_2nd_ret, 2);
	usb_mux_init(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 2);

	/* Test board init callback */
	proxy_chain_1.board_init = &mock_board_init;
	reset_proxy_fakes();
	usb_mux_init(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	/* Check if board_init was called for proxy 1 */
	zassert_equal(1, mock_board_init_fake.call_count, NULL);
	zassert_equal(&proxy_chain_1, mock_board_init_fake.arg0_history[0],
		      NULL);

	proxy_chain_1.board_init = NULL;
}

/** Test usb_mux setting mux mode */
ZTEST(usb_uninit_mux, test_usb_mux_set)
{
	int fail_on_2nd_ret[] = {EC_SUCCESS, EC_ERROR_UNKNOWN};
	mux_state_t exp_mode;

	/* Set flag for usb mux 1 to disable polarity setting */
	proxy_chain_1.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	/* Test setting mux mode without polarity inversion */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	/* All muxes should have the same mode */
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test setting mux mode with polarity inversion */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_TBT_COMPAT_ENABLED;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    1 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_set, NUM_OF_PROXY);
	/* usb mux 1 shouldn't be set with polarity mode, because of flag */
	zassert_equal(exp_mode | USB_PD_MUX_POLARITY_INVERTED,
		      proxy_set_fake.arg1_history[0], NULL);
	zassert_equal(exp_mode, proxy_set_fake.arg1_history[1], NULL);
	zassert_equal(exp_mode | USB_PD_MUX_POLARITY_INVERTED,
		      proxy_set_fake.arg1_history[2], NULL);

	/* Test board set callback */
	reset_proxy_fakes();
	proxy_chain_1.board_set = &mock_board_set;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
	/* Check if board_set was called for proxy 1 */
	zassert_equal(1, mock_board_set_fake.call_count, NULL);
	zassert_equal(&proxy_chain_1, mock_board_set_fake.arg0_history[0],
		      NULL);
	zassert_equal(exp_mode, mock_board_set_fake.arg1_history[0], NULL);

	/* Test set function with error in usb_mux */
	reset_proxy_fakes();
	SET_RETURN_SEQ(proxy_set, fail_on_2nd_ret, 2);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, 2, exp_mode);
	/* board_set shouldn't be called after fail */
	zassert_equal(0, mock_board_set_fake.call_count, NULL);

	proxy_chain_1.board_set = NULL;
}

/** Test usb_mux reset in g3 when required flag is set */
ZTEST(usb_uninit_mux, test_usb_mux_reset_in_g3)
{
	mux_state_t exp_mode = USB_PD_MUX_USB_ENABLED;

	/* Test that init is called */
	reset_proxy_fakes();
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Usb muxes of port 1 should stay initialised */
	usb_muxes[USBC_PORT_C1].flags = 0;
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	/* Test that init is not called */
	reset_proxy_fakes();
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
}

/** Test usb_mux getting mux mode */
ZTEST(usb_uninit_mux, test_usb_mux_get)
{
	int fail_on_2nd_ret[] = {EC_SUCCESS, EC_ERROR_UNKNOWN};
	mux_state_t exp_mode, mode;

	/* Test getting mux mode */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	set_proxy_get_mux_state_seq(exp_mode);
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);

	/* Test getting mux mode with inverted polarisation in one mux */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_TBT_COMPAT_ENABLED;
	set_proxy_get_mux_state_seq(exp_mode);
	/* Set polarisation in usb mux 1 state */
	proxy_get_mux_state_seq[1] |= USB_PD_MUX_POLARITY_INVERTED;
	exp_mode |= USB_PD_MUX_POLARITY_INVERTED;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);

	/* Test get function with error in usb_mux */
	reset_proxy_fakes();
	SET_RETURN_SEQ(proxy_get, fail_on_2nd_ret, 2);
	set_proxy_get_mux_state_seq(USB_PD_MUX_TBT_COMPAT_ENABLED);
	exp_mode = USB_PD_MUX_NONE;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, 2);
}

/** Test usb_mux entering and exiting low power mode */
ZTEST(usb_init_mux, test_usb_mux_low_power_mode)
{
	int fail_on_2nd_ret[] = {EC_SUCCESS, EC_ERROR_NOT_POWERED};
	mux_state_t exp_mode, mode;

	/* Test enter to low power mode */
	exp_mode = USB_PD_MUX_NONE;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_enter_low_power_mode, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test that nothing is changed when already in low power mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_NONE;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_enter_low_power_mode, 0);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_set, 0);

	/* Test that get return USB_PD_MUX_NONE in low power mode */
	exp_mode = USB_PD_MUX_NONE;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, 0);

	/* Test exiting from low power mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test exiting from lpm, when init end with EC_ERROR_NOT_POWERED */
	reset_proxy_fakes();
	SET_RETURN_SEQ(proxy_init, fail_on_2nd_ret, 2);
	usb_mux_init(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 2);

	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test enter to low power mode with polarity */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_NONE;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    1 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_enter_low_power_mode, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test that nothing is changed on lpm exit error */
	reset_proxy_fakes();
	SET_RETURN_SEQ(proxy_init, fail_on_2nd_ret, 2);
	exp_mode = USB_PD_MUX_USB_ENABLED;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 2);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_set, 0);
}

/** Test usb_mux flip */
ZTEST(usb_uninit_mux, test_usb_mux_flip)
{
	mux_state_t exp_mode;

	/* Set flag for usb mux 1 to disable polarity setting */
	proxy_chain_1.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	/* Test flip port without polarity inverted */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	set_proxy_get_mux_state_seq(exp_mode);
	usb_mux_flip(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_set, NUM_OF_PROXY);
	/* usb mux 1 shouldn't be set with polarity mode, because of flag */
	zassert_equal(exp_mode | USB_PD_MUX_POLARITY_INVERTED,
		      proxy_set_fake.arg1_history[0], NULL);
	zassert_equal(exp_mode, proxy_set_fake.arg1_history[1], NULL);
	zassert_equal(exp_mode | USB_PD_MUX_POLARITY_INVERTED,
		      proxy_set_fake.arg1_history[2], NULL);

	/* Test flip port with polarity inverted */
	reset_proxy_fakes();
	exp_mode |= USB_PD_MUX_POLARITY_INVERTED;
	set_proxy_get_mux_state_seq(exp_mode);
	/* Clear polarity bit from usb mux 1 */
	proxy_get_mux_state_seq[1] &= ~USB_PD_MUX_POLARITY_INVERTED;
	exp_mode &= ~USB_PD_MUX_POLARITY_INVERTED;
	usb_mux_flip(USBC_PORT_C1);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
}

ZTEST(usb_uninit_mux, test_usb_mux_hpd_update)
{
	mux_state_t exp_mode, mode, virt_mode;

	/* Get current state of virtual usb mux and set mock */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &virt_mode);

	/* Test no hpd level and no irq */
	exp_mode = virt_mode;
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_hpd_update, NUM_OF_PROXY,
					    exp_mode);

	/* Test hpd level and irq */
	reset_proxy_fakes();
	exp_mode = virt_mode | USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ;
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_hpd_update, NUM_OF_PROXY,
					    exp_mode);

	/* Test no hpd level and irq */
	reset_proxy_fakes();
	exp_mode = virt_mode | USB_PD_MUX_HPD_IRQ;
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_hpd_update, NUM_OF_PROXY,
					    exp_mode);

	/* Test hpd level and no irq */
	reset_proxy_fakes();
	exp_mode = virt_mode | USB_PD_MUX_HPD_LVL;
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, 0);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_hpd_update, NUM_OF_PROXY,
					    exp_mode);

	/* Test ps8xxx hpd update */
	usb_muxes[USBC_PORT_C1].usb_port = 1;
	usb_muxes[USBC_PORT_C1].driver = &tcpci_tcpm_usb_mux_driver;
	usb_muxes[USBC_PORT_C1].hpd_update = &ps8xxx_tcpc_update_hpd_status;

	reset_proxy_fakes();
	exp_mode = virt_mode | USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ;
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if PS8xxx mux mode is updated correctly */
	tcpci_tcpm_usb_mux_driver.get(&usb_muxes[USBC_PORT_C1], &mode);
	zassert_equal(0, mode, "mux mode is 0x%x (!= 0x%x)",
		      mode, 0);
}

ZTEST(usb_init_mux, test_usb_mux_fw_update_port_info)
{
	int port_info;

	port_info = usb_mux_retimer_fw_update_port_info();
	zassert_true(port_info & BIT(USBC_PORT_C1),
		     "fw update for port C1 should be set");
}

ZTEST(usb_init_mux, test_usb_mux_chipset_reset)
{
	/* After this hook chipset reset functions should be called */
	hook_notify(HOOK_CHIPSET_RESET);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_chipset_reset, NUM_OF_PROXY);
}

/* Test host command get mux info */
ZTEST(usb_init_mux, test_usb_mux_hc_mux_info)
{
	struct ec_response_usb_pd_mux_info response;
	struct ec_params_usb_pd_mux_info params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_USB_PD_MUX_INFO, 0, response, params);
	mux_state_t exp_mode;

	/* Test invalid port parameter */
	params.port = 5;
	zassert_equal(EC_RES_INVALID_PARAM, host_command_process(&args), NULL);

	/* Set correct port for rest of the test */
	params.port = USBC_PORT_C1;

	/* Test error on getting mux mode */
	set_proxy_get_mux_state_seq(USB_PD_MUX_USB_ENABLED);
	proxy_get_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(EC_RES_ERROR, host_command_process(&args), NULL);

	/* Test getting mux mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED;
	set_proxy_get_mux_state_seq(exp_mode);
	zassert_equal(EC_RES_SUCCESS, host_command_process(&args), NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(exp_mode, response.flags, "mode is 0x%x (!= 0x%x)",
		      response.flags, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);

	/* Test clearing HPD IRQ */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_HPD_LVL |
		   USB_PD_MUX_HPD_IRQ;
	set_proxy_get_mux_state_seq(exp_mode);
	zassert_equal(EC_RES_SUCCESS, host_command_process(&args), NULL);
	zassert_equal(args.response_size, sizeof(response), NULL);
	zassert_equal(exp_mode, response.flags, "mode is 0x%x (!= 0x%x)",
		      response.flags, exp_mode);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_hpd_update, NUM_OF_PROXY,
					    USB_PD_MUX_HPD_LVL);
}

/** Test typec console command */
ZTEST(usb_init_mux, test_usb_mux_typec_command)
{
	mux_state_t polarity;
	mux_state_t exp_mode;

	/* Test error on command with no argument */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(),
					"typec"), NULL);

	/*
	 * Test success on passing "debug" as first argument. This will enable
	 * debug prints, but it is not possible to test that in unit test
	 * without accessing cprints output.
	 */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec debug"), NULL);

	/* Test error on port argument that is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(),
					"typec test1"), NULL);

	/* Test error on invalid port number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(),
					"typec 5"), NULL);

	/*
	 * Test success on correct port number. Command should print mux state
	 * on console, but it is not possible to check that in unit test.
	 */
	set_proxy_get_mux_state_seq(USB_PD_MUX_TBT_COMPAT_ENABLED);
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec 1"), NULL);
	CHECK_PROXY_FAKE_CALL_CNT(proxy_get, NUM_OF_PROXY);

	/* Test setting none mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_NONE;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec 1 none"), NULL);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
	/* Mux will enter low power mode */
	CHECK_PROXY_FAKE_CALL_CNT(proxy_enter_low_power_mode, NUM_OF_PROXY);

	/* Polarity is set based on PD */
	polarity = polarity_rm_dts(pd_get_polarity(USBC_PORT_C1)) ?
			USB_PD_MUX_POLARITY_INVERTED : 0;

	/* Test setting USB mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED | polarity;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec 1 usb"), NULL);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
	/* Mux will exit low power mode */
	CHECK_PROXY_FAKE_CALL_CNT(proxy_init, NUM_OF_PROXY);

	/* Test setting DP mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_DP_ENABLED | polarity;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec 1 dp"), NULL);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);

	/* Test setting dock mode */
	reset_proxy_fakes();
	exp_mode = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED | polarity;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(),
					"typec 1 dock"), NULL);
	CHECK_PROXY_FAKE_CALL_CNT_MUX_STATE(proxy_set, NUM_OF_PROXY, exp_mode);
}

/** Setup proxy chain and uninit usb muxes */
void usb_uninit_mux_before(void *state)
{
	ARG_UNUSED(state);
	setup_usb_mux_proxy_chain();
	set_test_runner_tid();

	/* Makes sure that usb muxes of port 1 are not init */
	usb_muxes[USBC_PORT_C1].flags = USB_MUX_FLAG_RESETS_IN_G3;
	hook_notify(HOOK_CHIPSET_HARD_OFF);
	reset_proxy_fakes();
}

void usb_uninit_mux_after(void *state)
{
	ARG_UNUSED(state);
	restore_usb_mux_chain();
}

/** Setup proxy chain and init usb muxes */
void usb_init_mux_before(void *state)
{
	ARG_UNUSED(state);
	setup_usb_mux_proxy_chain();
	set_test_runner_tid();

	/* Makes sure that usb muxes of port 1 are init */
	usb_mux_init(USBC_PORT_C1);
	reset_proxy_fakes();
}

void usb_init_mux_after(void *state)
{
	ARG_UNUSED(state);
	restore_usb_mux_chain();
}

ZTEST_SUITE(usb_uninit_mux, drivers_predicate_post_main, NULL,
	    usb_uninit_mux_before, usb_uninit_mux_after, NULL);

ZTEST_SUITE(usb_init_mux, drivers_predicate_post_main, NULL,
	    usb_init_mux_before, usb_init_mux_after, NULL);
