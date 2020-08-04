/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB-PD interrupt task.
 */
#include "task.h"
#include "test_util.h"
#include "mock/tcpc_mock.h"
#include "mock/timer_mock.h"
#include "mock/usb_mux_mock.h"

#define PORT0 0

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

void board_reset_pd_mcu(void)
{
}

static int deferred_resume_called;
void pd_deferred_resume(int port)
{
		deferred_resume_called = 1;
}

static int num_events;
uint16_t tcpc_get_alert_status(void)
{
		if (--num_events > 0)
			return PD_STATUS_TCPC_ALERT_0;
		else
			return 0;
}

test_static int test_storm_not_triggered(void)
{
		num_events = 100;
		deferred_resume_called = 0;
		schedule_deferred_pd_interrupt(PORT0);
		task_wait_event(SECOND);
		TEST_EQ(deferred_resume_called, 0, "%d");

		return EC_SUCCESS;
}

test_static int test_storm_triggered(void)
{
		num_events = 1000;
		deferred_resume_called = 0;
		schedule_deferred_pd_interrupt(PORT0);
		task_wait_event(SECOND);
		TEST_EQ(deferred_resume_called, 1, "%d");

		return EC_SUCCESS;
}

test_static int test_storm_not_triggered_for_32bit_overflow(void)
{
		int i;
		timestamp_t time;

		/* Ensure the MSB is 1 for overflow comparison tests */
		time.val = 0xff000000;
		force_time(time);

		/*
		 * 100 events every second for 10 seconds should never trigger
		 * a shutdown call.
		 */
		for (i = 0; i < 10; ++i) {
			num_events = 100;
			deferred_resume_called = 0;
			schedule_deferred_pd_interrupt(PORT0);
			task_wait_event(SECOND);

			TEST_EQ(deferred_resume_called, 0, "%d");
		}

		return EC_SUCCESS;
}

void before_test(void)
{
		pd_set_suspend(PORT0, 0);
}

void run_test(int argc, char **argv)
{
	/* Let tasks settle down */
	task_wait_event(MINUTE);

	RUN_TEST(test_storm_not_triggered);
	RUN_TEST(test_storm_triggered);
	RUN_TEST(test_storm_not_triggered_for_32bit_overflow);

	test_print_result();
}
