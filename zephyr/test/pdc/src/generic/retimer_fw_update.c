/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "test/util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc/pd_task_intel_altmode.h"
#include "usbc/retimer_fw_update.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define PORT 0

FAKE_VALUE_FUNC(bool, is_pd_intel_altmode_task_suspended);
FAKE_VOID_FUNC(suspend_pd_intel_altmode_task);
FAKE_VOID_FUNC(resume_pd_intel_altmode_task);

int pd_retimer_state_init(void);

static void before(void *unused)
{
	ARG_UNUSED(unused);

	RESET_FAKE(is_pd_intel_altmode_task_suspended);
	RESET_FAKE(suspend_pd_intel_altmode_task);
	RESET_FAKE(resume_pd_intel_altmode_task);

	pd_retimer_state_init();
}

ZTEST_SUITE(retimer_fw_update, NULL, NULL, before, NULL, NULL);

ZTEST_USER(retimer_fw_update, test_query)
{
	/* Query port returns a bitfield of fw update enabled ports. */
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_QUERY_PORT);
	zassert_equal(usb_retimer_fw_update_get_result(), BIT(0) | BIT(1));
}

ZTEST_USER(retimer_fw_update, test_get)
{
	/* Get mux should be called with the mux online. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_GET_MUX);
	zassert_equal(usb_retimer_fw_update_get_result(), USB_PD_MUX_NONE);
}

ZTEST_USER(retimer_fw_update, test_suspend_alt_mode_failure)
{
	is_pd_intel_altmode_task_suspended_fake.return_val = false;
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_SUSPEND_PD);
	zassert_not_ok(usb_retimer_fw_update_get_result());
}

ZTEST_USER(retimer_fw_update, test_suspend_failure)
{
	/* Suspend successfully. */
	is_pd_intel_altmode_task_suspended_fake.return_val = true;
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_SUSPEND_PD);
	zassert_ok(usb_retimer_fw_update_get_result());

	/* Attempt to suspend while suspended should fail. */
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_SUSPEND_PD);
	zassert_not_ok(usb_retimer_fw_update_get_result());

	/* Get mux requires the retimer is online. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_GET_MUX);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}

static void enter_tbt(void *unused)
{
	k_msleep(300);
	usb_mux_set(PORT, USB_PD_MUX_TBT_COMPAT_ENABLED, USB_SWITCH_CONNECT,
		    pd_get_polarity(PORT));
}

ZTEST_USER(retimer_fw_update, test_update)
{
	struct k_work tbt_mode_entry_work;
	/* Polarity shouldn't change. */
	const enum tcpc_cc_polarity polarity = pd_get_polarity(PORT);

	k_work_init(&tbt_mode_entry_work, enter_tbt);

	is_pd_intel_altmode_task_suspended_fake.return_val = true;
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_SUSPEND_PD);
	zassert_equal(suspend_pd_intel_altmode_task_fake.call_count, 1);
	zassert_ok(usb_retimer_fw_update_get_result());

	/* Test USB mode. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_USB);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_PD_MUX_USB_ENABLED);
	zassert_equal(pd_get_polarity(PORT), polarity);

	/* Test safe mode. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_SAFE);
	zassert_equal(usb_retimer_fw_update_get_result(), USB_PD_MUX_SAFE_MODE);
	zassert_equal(pd_get_polarity(PORT), polarity);

	/* Test TBT, this will trigger the update proccess. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_TBT);
	/*
	 * Per comments and CL notes, the AP is responsible for configuring the
	 * mux without EC input here. Simulate that.
	 */
	k_work_submit(&tbt_mode_entry_work);
	zassert_true(TEST_WAIT_FOR(usb_retimer_fw_update_get_result() ==
					   USB_PD_MUX_TBT_COMPAT_ENABLED,
				   1000));
	zassert_equal(pd_get_polarity(PORT), polarity);
	/* FW update triggers alt-mode changes. */
	zassert_equal(resume_pd_intel_altmode_task_fake.call_count, 1);

	/* Disconnect. */
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_DISCONNECT);
	zassert_equal(usb_retimer_fw_update_get_result(), USB_PD_MUX_NONE);
	zassert_equal(pd_get_polarity(PORT), polarity);

	/* Resume. */
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_RESUME_PD);
	zassert_true(
		TEST_WAIT_FOR(usb_retimer_fw_update_get_result() == 1, 1000));
}

ZTEST_USER(retimer_fw_update, test_online_usb_failure)
{
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_USB);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}

ZTEST_USER(retimer_fw_update, test_online_safe_failure)
{
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_SAFE);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}

ZTEST_USER(retimer_fw_update, test_online_tbt_failure)
{
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_SET_TBT);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}

ZTEST_USER(retimer_fw_update, test_online_disconnect_failure)
{
	usb_retimer_fw_update_process_op(PORT,
					 USB_RETIMER_FW_UPDATE_DISCONNECT);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}

ZTEST_USER(retimer_fw_update, test_online_resume_failure)
{
	usb_retimer_fw_update_process_op(PORT, USB_RETIMER_FW_UPDATE_RESUME_PD);
	zassert_equal(usb_retimer_fw_update_get_result(),
		      USB_RETIMER_FW_UPDATE_ERR);
}
