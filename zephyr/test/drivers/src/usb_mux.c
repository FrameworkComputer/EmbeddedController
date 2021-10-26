/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>

#include "common.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "i2c.h"
#include "stubs.h"
#include "task.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"

#include "usb_mux.h"

/** Copy of original usb_muxes[USB_PORT_C1] */
struct usb_mux usb_mux_c1;

/** Pointers to original usb muxes chain of port c1 */
const struct usb_mux *org_mux[3];

/** Proxy function which check calls from usb_mux framework to driver */
static int proxy_init(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->init != NULL) {
		org_mux[i]->driver->init(org_mux[i]);
	}

	return ztest_get_return_value();
}

/** Proxy function which check calls from usb_mux framework to driver */
static int proxy_set(const struct usb_mux *me, mux_state_t mux_state,
		     bool *ack_required)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);
	ztest_check_expected_value(mux_state);

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->set != NULL) {
		org_mux[i]->driver->set(org_mux[i], mux_state, ack_required);
	}

	return ztest_get_return_value();
}

/** Proxy function which check calls from usb_mux framework to driver */
static int proxy_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->get != NULL) {
		org_mux[i]->driver->get(org_mux[i], mux_state);
	}

	*mux_state = ztest_get_return_value();

	return ztest_get_return_value();
}

/** Proxy function which check calls from usb_mux framework to driver */
static int proxy_enter_low_power_mode(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->enter_low_power_mode != NULL) {
		org_mux[i]->driver->enter_low_power_mode(org_mux[i]);
	}

	return ztest_get_return_value();
}

/** Proxy function which check calls from usb_mux framework to driver */
static int proxy_chipset_reset(const struct usb_mux *me)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);

	if (org_mux[i] != NULL &&
	    org_mux[i]->driver->chipset_reset != NULL) {
		org_mux[i]->driver->chipset_reset(org_mux[i]);
	}

	return ztest_get_return_value();
}

/** Proxy function for fw update capability */
static bool proxy_fw_update_cap(void)
{
	return true;
}

/** Proxy function which check calls from usb_mux framework to driver */
static void proxy_hpd_update(const struct usb_mux *me, mux_state_t mux_state)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);
	ztest_check_expected_value(mux_state);

	if (org_mux[i] != NULL &&
	    org_mux[i]->hpd_update != NULL) {
		org_mux[i]->hpd_update(org_mux[i], mux_state);
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
	for (i = 0; i < 3; i++) {
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

static void suspend_usbc_task(bool suspend)
{
	static const task_id_t cros_tids[] = {
		COND_CODE_1(HAS_TASK_PD_C0, (TASK_ID_PD_C0,), ())
		COND_CODE_1(HAS_TASK_PD_C1, (TASK_ID_PD_C1,), ())
		COND_CODE_1(HAS_TASK_PD_C2, (TASK_ID_PD_C2,), ())
		COND_CODE_1(HAS_TASK_PD_C3, (TASK_ID_PD_C3,), ())
	};

	for (int i = 0; i < ARRAY_SIZE(cros_tids); ++i)
		/*
		 * TODO(b/201420132): pd_set_suspend uses sleeps which we should
		 * minimize
		 */
		pd_set_suspend(TASK_ID_TO_PD_PORT(cros_tids[i]), suspend);
}

/** Restore original usb_mux chain without proxy */
static void resotre_usb_mux_chain(void)
{
	suspend_usbc_task(/*suspend=*/ false);
	memcpy(&usb_muxes[USBC_PORT_C1], &usb_mux_c1, sizeof(struct usb_mux));
}

/**
 * Setup expect values for proxy from first to last selected.
 * Set value returned by proxy to ec.
 */
static void setup_ztest_proxy_init(int first, int last, int ec)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_init, i, i);
		ztest_returns_value(proxy_init, ec);
	}
}

/**
 * Setup expect values for proxy from first to last selected.
 * Set value returned by proxy to ec.
 */
static void setup_ztest_proxy_set(int first, int last, int ec, mux_state_t exp)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_set, i, i);
		ztest_expect_value(proxy_set, mux_state, exp);
		ztest_returns_value(proxy_set, ec);
	}
}

/**
 * Setup expect values for proxy from first to last selected. Set value
 * returned by proxy to ec and value returned through mux_state to exp.
 */
static void setup_ztest_proxy_get(int first, int last, int ec, mux_state_t exp)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_get, i, i);
		ztest_returns_value(proxy_get, exp);
		ztest_returns_value(proxy_get, ec);
	}
}

/**
 * Setup expect values for proxy from first to last selected.
 * Set value returned by proxy to ec.
 */
static void setup_ztest_proxy_enter_lpm(int first, int last, int ec)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_enter_low_power_mode, i, i);
		ztest_returns_value(proxy_enter_low_power_mode, ec);
	}
}

/**
 * Setup expect values for proxy from first to last selected.
 * Set value returned by proxy to ec.
 */
static void setup_ztest_proxy_chipset_reset(int first, int last, int ec)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_chipset_reset, i, i);
		ztest_returns_value(proxy_chipset_reset, ec);
	}
}

/** Setup expect values for proxy from first to last selected */
static void setup_ztest_proxy_hpd_update(int first, int last, mux_state_t exp)
{
	for (int i = first; i <= last; i++) {
		ztest_expect_value(proxy_hpd_update, i, i);
		ztest_expect_value(proxy_hpd_update, mux_state, exp);
	}
}

/** Mock function used in set test */
static int mock_board_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int i = me->i2c_addr_flags;

	ztest_check_expected_value(i);

	return EC_SUCCESS;
}

/** Test usb_mux init */
static void test_usb_mux_init(void)
{
	/* Set AP to normal state to init BB retimer */
	set_mock_power_state(POWER_S0);
	/*
	 * TODO(b/201420132) - setting power state requires to wake up
	 * TASK_ID_CHIPSET Sleep is required to run chipset task before
	 * continuing with test
	 */
	k_msleep(1);

	/* Test successful initialisation */
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	usb_mux_init(USBC_PORT_C1);

	/*
	 * Test failed initialisation. Muxes that are in chain after
	 * the one which fails shouldn't be called
	 */
	setup_ztest_proxy_init(0, 0, EC_SUCCESS);
	setup_ztest_proxy_init(1, 1, EC_ERROR_NOT_POWERED);
	usb_mux_init(USBC_PORT_C1);

	/* Test board init callback */
	proxy_chain_1.board_init = proxy_init;
	setup_ztest_proxy_init(0, 0, EC_SUCCESS);
	/*
	 * board_init of second mux mock is set to init mock function, so it
	 * should be called two times.
	 */
	setup_ztest_proxy_init(1, 1, EC_SUCCESS);
	setup_ztest_proxy_init(1, 2, EC_SUCCESS);

	usb_mux_init(USBC_PORT_C1);

	proxy_chain_1.board_init = NULL;
}

/** Test usb_mux setting mux mode */
static void test_usb_mux_set(void)
{
	mux_state_t exp_mode;

	/* usb mux 1 shouldn't be set with polarity mode */
	proxy_chain_1.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	/* Test setting mux mode without polarity inversion */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	/* Test setting mux mode with polarity inversion */
	exp_mode = USB_PD_MUX_TBT_COMPAT_ENABLED;
	setup_ztest_proxy_set(0, 0, EC_SUCCESS,
			      exp_mode | USB_PD_MUX_POLARITY_INVERTED);
	setup_ztest_proxy_set(1, 1, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_set(2, 2, EC_SUCCESS,
			      exp_mode | USB_PD_MUX_POLARITY_INVERTED);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    1 /* = polarity */);

	/* Test board set callback */
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	proxy_chain_1.board_set = &mock_board_set;
	ztest_expect_value(mock_board_set, i, 1);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	/* Test set function with error in usb_mux */
	setup_ztest_proxy_set(0, 0, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_set(1, 1, EC_ERROR_UNKNOWN, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	proxy_chain_1.board_set = NULL;
}

/** Test usb_mux reset in g3 when required flag is set */
static void test_usb_mux_reset_in_g3(void)
{
	mux_state_t exp_mode = USB_PD_MUX_USB_ENABLED;

	/* Test that init is called */
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	/* Usb muxes of port 1 should stay initialised */
	usb_muxes[USBC_PORT_C1].flags = 0;
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	/* Test that init is not called */
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
}

/** Test usb_mux getting mux mode */
static void test_usb_mux_get(void)
{
	mux_state_t exp_mode, mode;

	/* Test getting mux mode */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_get(0, 2, EC_SUCCESS, exp_mode);
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);

	/* Test getting mux mode with one inverted polarisation */
	exp_mode = USB_PD_MUX_TBT_COMPAT_ENABLED;
	setup_ztest_proxy_get(0, 0, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_get(1, 1, EC_SUCCESS,
			      exp_mode | USB_PD_MUX_POLARITY_INVERTED);
	setup_ztest_proxy_get(2, 2, EC_SUCCESS, exp_mode);
	exp_mode |= USB_PD_MUX_POLARITY_INVERTED;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);

	/* Test get function with error in usb_mux */
	setup_ztest_proxy_get(0, 0, EC_SUCCESS, USB_PD_MUX_TBT_COMPAT_ENABLED);
	setup_ztest_proxy_get(1, 1, EC_ERROR_UNKNOWN,
			      USB_PD_MUX_TBT_COMPAT_ENABLED);
	exp_mode = USB_PD_MUX_NONE;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);
}

/** Test usb_mux entering and exiting low power mode */
static void test_usb_mux_low_power_mode(void)
{
	mux_state_t exp_mode, mode;

	/*
	 * Virtual mux return ack_required in some cases, but this requires to
	 * run usb_mux_set in TASK_PD_C1 context. Remove virtual mux from chain
	 * for this test.
	 *
	 * TODO: Find way to setup PD stack in such state that notifing PD task
	 *       results in required usb_mux_set call.
	 */
	org_mux[1] = NULL;

	/* Test enter to low power mode */
	exp_mode = USB_PD_MUX_NONE;
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_enter_lpm(0, 2, EC_SUCCESS);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    0 /* = polarity */);

	/* Test that nothing is changed when already in low power mode */
	exp_mode = USB_PD_MUX_NONE;
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    0 /* = polarity */);

	/* Test that get return USB_PD_MUX_NONE in low power mode */
	exp_mode = USB_PD_MUX_NONE;
	mode = usb_mux_get(USBC_PORT_C1);
	zassert_equal(exp_mode, mode, "mode is 0x%x (!= 0x%x)", mode, exp_mode);

	/* Test exiting from low power mode */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	/* Test exiting from lpm, when init end with EC_ERROR_NOT_POWERED */
	setup_ztest_proxy_init(0, 0, EC_SUCCESS);
	setup_ztest_proxy_init(1, 1, EC_ERROR_NOT_POWERED);
	usb_mux_init(USBC_PORT_C1);

	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);

	/* Test enter to low power mode with polarity */
	exp_mode = USB_PD_MUX_NONE;
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_enter_lpm(0, 2, EC_SUCCESS);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_DISCONNECT,
		    1 /* = polarity */);

	/* Test that nothing is changed on lpm exit error */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 0, EC_SUCCESS);
	setup_ztest_proxy_init(1, 1, EC_ERROR_NOT_POWERED);
	usb_mux_set(USBC_PORT_C1, exp_mode, USB_SWITCH_CONNECT,
		    0 /* = polarity */);
}

/** Test usb_mux flip */
static void test_usb_mux_flip(void)
{
	mux_state_t exp_mode;

	/* usb mux mock 1 shouldn't be set with polarity mode */
	proxy_chain_1.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	/* Makes sure that usb muxes of port 1 are not init */
	usb_muxes[USBC_PORT_C1].flags = USB_MUX_FLAG_RESETS_IN_G3;
	hook_notify(HOOK_CHIPSET_HARD_OFF);

	/* Test flip port without polarity inverted */
	exp_mode = USB_PD_MUX_USB_ENABLED;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_get(0, 2, EC_SUCCESS, exp_mode);
	exp_mode |= USB_PD_MUX_POLARITY_INVERTED;
	setup_ztest_proxy_set(0, 0, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_set(1, 1, EC_SUCCESS,
			      exp_mode & ~USB_PD_MUX_POLARITY_INVERTED);
	setup_ztest_proxy_set(2, 2, EC_SUCCESS, exp_mode);
	usb_mux_flip(USBC_PORT_C1);

	/* Test flip port with polarity inverted */
	setup_ztest_proxy_get(0, 0, EC_SUCCESS, exp_mode);
	setup_ztest_proxy_get(1, 1, EC_SUCCESS,
			      exp_mode & ~USB_PD_MUX_POLARITY_INVERTED);
	setup_ztest_proxy_get(2, 2, EC_SUCCESS, exp_mode);
	exp_mode &= ~USB_PD_MUX_POLARITY_INVERTED;
	setup_ztest_proxy_set(0, 2, EC_SUCCESS, exp_mode);
	usb_mux_flip(USBC_PORT_C1);
}

void test_usb_mux_hpd_update(void)
{
	mux_state_t exp_mode, mode, virt_mode;

	/* Get current state of virtual usb mux and set mock */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &virt_mode);

	/* Test no hpd level and no irq */
	exp_mode = virt_mode;
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	setup_ztest_proxy_hpd_update(0, 2, exp_mode);
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);

	/* Test hpd level and irq */
	exp_mode = virt_mode | USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ;
	setup_ztest_proxy_hpd_update(0, 2, exp_mode);
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);

	/* Test no hpd level and irq */
	exp_mode = virt_mode | USB_PD_MUX_HPD_IRQ;
	setup_ztest_proxy_hpd_update(0, 2, exp_mode);
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);

	/* Test hpd level and no irq */
	exp_mode = virt_mode | USB_PD_MUX_HPD_LVL;
	setup_ztest_proxy_hpd_update(0, 2, exp_mode);
	usb_mux_hpd_update(USBC_PORT_C1, exp_mode);
	/* Check if virtual usb mux mode is updated correctly */
	usbc1_virtual_usb_mux.driver->get(&usbc1_virtual_usb_mux, &mode);
	zassert_equal(exp_mode, mode, "virtual mux mode is 0x%x (!= 0x%x)",
		      mode, exp_mode);
}

void test_usb_mux_fw_update_port_info(void)
{
	int port_info;

	port_info = usb_mux_retimer_fw_update_port_info();
	zassert_true(port_info & BIT(USBC_PORT_C1),
		     "fw update for port C1 should be set");
}

void test_usb_mux_chipset_reset(void)
{
	setup_ztest_proxy_chipset_reset(0, 2, EC_SUCCESS);
	/* After this hook chipset reset functions should be called */
	hook_notify(HOOK_CHIPSET_RESET);
}

/** Setup proxy chain and uninit usb muxes */
void setup_uninit_mux(void)
{
	suspend_usbc_task(/*suspend=*/ true);
	setup_usb_mux_proxy_chain();

	/* Makes sure that usb muxes of port 1 are not init */
	usb_muxes[USBC_PORT_C1].flags = USB_MUX_FLAG_RESETS_IN_G3;
	hook_notify(HOOK_CHIPSET_HARD_OFF);
}

/** Setup proxy chain and init usb muxes */
void setup_init_mux(void)
{
	suspend_usbc_task(/*suspend=*/ true);
	setup_usb_mux_proxy_chain();

	/* Makes sure that usb muxes of port 1 are init */
	setup_ztest_proxy_init(0, 2, EC_SUCCESS);
	usb_mux_init(USBC_PORT_C1);
}

void test_suite_usb_mux(void)
{
	ztest_test_suite(usb_mux,
			 ztest_unit_test_setup_teardown(test_usb_mux_init,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(test_usb_mux_set,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(
				test_usb_mux_reset_in_g3,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(test_usb_mux_get,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(
				test_usb_mux_low_power_mode,
				setup_init_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(test_usb_mux_flip,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(test_usb_mux_hpd_update,
				setup_uninit_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(
				test_usb_mux_fw_update_port_info,
				setup_init_mux, resotre_usb_mux_chain),
			 ztest_unit_test_setup_teardown(
				test_usb_mux_chipset_reset,
				setup_init_mux, resotre_usb_mux_chain));
	ztest_run_test_suite(usb_mux);
}
