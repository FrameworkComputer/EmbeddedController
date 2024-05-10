/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PD timer module.
 */
#include "atomic.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd_timer.h"

/*
 * Verify the bit operations and make sure another port is not affected
 */
int verify_pd_timers_bit_ops(int prim_port, int sec_port)
{
	for (int bit = 0; bit < PD_TIMER_COUNT; ++bit) {
		/* Check the initial state */
		TEST_EQ(PD_CHK_ACTIVE(prim_port, bit), 0, "%d");
		TEST_EQ(PD_CHK_ACTIVE(sec_port, bit), 0, "%d");
		PD_SET_ACTIVE(prim_port, bit);
		for (int i = 0; i < PD_TIMER_COUNT; ++i) {
			if (i != bit)
				TEST_EQ(PD_CHK_ACTIVE(prim_port, i), 0, "%d");
			else
				TEST_NE(PD_CHK_ACTIVE(prim_port, i), 0, "%d");

			/* Make sure the second port is not affected. */
			TEST_EQ(PD_CHK_ACTIVE(sec_port, i), 0, "%d");
		}
		PD_CLR_ACTIVE(prim_port, bit);
	}

	/*
	 * Clear one disabled bit at a time and verify it is the only
	 * bit clear. Reset the bit on each iteration of the bit loop.
	 */
	for (int bit = 0; bit < PD_TIMER_COUNT; ++bit) {
		/* Check the initial state */
		TEST_NE(PD_CHK_DISABLED(prim_port, bit), 0, "%d");
		TEST_NE(PD_CHK_DISABLED(sec_port, bit), 0, "%d");
		PD_CLR_DISABLED(prim_port, bit);
		for (int i = 0; i < PD_TIMER_COUNT; ++i) {
			if (i != bit)
				TEST_NE(PD_CHK_DISABLED(prim_port, i), 0, "%d");
			else
				TEST_EQ(PD_CHK_DISABLED(prim_port, i), 0, "%d");

			/* Make sure the second port is not affected. */
			TEST_NE(PD_CHK_DISABLED(sec_port, i), 0, "%d");
		}
		PD_SET_DISABLED(prim_port, bit);
	}

	return EC_SUCCESS;
}

/*
 * Verify the init operation of PD timers.
 */
int test_pd_timers_init(void)
{
	int bit;
	int prim_port, sec_port;

	/*
	 * Initialization calling pd_timer_init will initialize the port's
	 * active timer to be clear and disabled timer to be set for all mask
	 * bits
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		prim_port = port;
		sec_port = (port + 1) % CONFIG_USB_PD_PORT_MAX_COUNT;
		pd_timer_init(prim_port);
		for (bit = 0; bit < PD_TIMER_COUNT; ++bit)
			TEST_EQ(PD_CHK_ACTIVE(prim_port, bit), 0, "%d");
		for (bit = 0; bit < PD_TIMER_COUNT; ++bit)
			TEST_NE(PD_CHK_DISABLED(prim_port, bit), 0, "%d");

		/*
		 * Make sure pd_timer_init(sec_port) doesn't affect other ports
		 */
		for (bit = 0; bit < PD_TIMER_COUNT; ++bit) {
			PD_SET_ACTIVE(prim_port, bit);
			PD_CLR_DISABLED(prim_port, bit);
		}
		pd_timer_init(sec_port);
		for (bit = 0; bit < PD_TIMER_COUNT; ++bit) {
			TEST_NE(PD_CHK_ACTIVE(prim_port, bit), 0, "%d");
			TEST_EQ(PD_CHK_DISABLED(prim_port, bit), 0, "%d");
		}
	}

	return EC_SUCCESS;
}

/*
 * Verify the operation of the underlying bit operations underlying the timer
 * module. This is technically redundant with the higher level test below, but
 * it is useful for catching bugs during timer changes.
 */
int test_pd_timers_bit_ops(void)
{
	int prim_port, sec_port;

	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		prim_port = port;
		sec_port = (port + 1) % CONFIG_USB_PD_PORT_MAX_COUNT;

		pd_timer_init(prim_port);
		pd_timer_init(sec_port);

		verify_pd_timers_bit_ops(prim_port, sec_port);
	}

	return EC_SUCCESS;
}

int test_pd_timers(void)
{
	int bit;
	int ms_to_expire;
	const int port = 0;

	/*
	 * Initialization calling pd_timer_init will initialize the port's
	 * active timer to be clear and disabled timer to be set for all mask
	 * bits.
	 */
	pd_timer_init(port);

	/* Verify all timers are disabled. */
	for (bit = 0; bit < PD_TIMER_COUNT; ++bit)
		TEST_ASSERT(pd_timer_is_disabled(port, bit));

	/* Enable some timers. */
	for (bit = 0; bit < 5; ++bit)
		pd_timer_enable(0, bit, (bit + 1) * 50);

	/* Verify all timers for enabled/disabled. */
	for (bit = 0; bit < PD_TIMER_COUNT; ++bit) {
		if (bit < 5)
			TEST_ASSERT(!pd_timer_is_disabled(port, bit));
		else
			TEST_ASSERT(pd_timer_is_disabled(port, bit));
	}

	/* Disable the first timer; verify all timers for enabled/disabled. */
	pd_timer_disable(port, 0);
	TEST_ASSERT(pd_timer_is_disabled(port, 0));
	for (bit = 1; bit < 5; ++bit)
		TEST_ASSERT(!pd_timer_is_disabled(port, bit));
	for (; bit < PD_TIMER_COUNT; ++bit)
		TEST_ASSERT(pd_timer_is_disabled(port, bit));

	/*
	 * Verify finding the next timer to expire.
	 *
	 * Timer at BIT(1) is the next to expire and originally had an expire
	 * time of 100ms. So allow for the test's simulated time lapse and
	 * verify in the 90-100 range.
	 */
	ms_to_expire = pd_timer_next_expiration(port);
	TEST_GE(ms_to_expire, 90, "%d");
	TEST_LE(ms_to_expire, 100, "%d");

	/* Enable the timers in the PRL range. */
	for (bit = PR_TIMER_START; bit <= PR_TIMER_END; ++bit)
		pd_timer_enable(port, bit, 20);

	/* Verify all timers for enabled/disabled. */
	for (bit = 0; bit < PD_TIMER_COUNT; ++bit) {
		if ((bit > 0 && bit < 5) ||
		    (bit >= PR_TIMER_START && bit <= PR_TIMER_END))
			TEST_ASSERT(!pd_timer_is_disabled(port, bit));
		else
			TEST_ASSERT(pd_timer_is_disabled(port, bit));
	}
	/* Verify that the PRL timers haven't expired yet. */
	for (bit = PR_TIMER_START; bit <= PR_TIMER_END; ++bit)
		TEST_ASSERT(!pd_timer_is_expired(port, bit));

	/* Allow the PRL timers to expire and verify that they have expired. */
	crec_msleep(21);
	for (bit = PR_TIMER_START; bit <= PR_TIMER_END; ++bit)
		TEST_ASSERT(pd_timer_is_expired(port, bit));

	/* Disable the PRL range. */
	pd_timer_disable_range(port, PR_TIMER_RANGE);
	/* Verify all timers for enabled/disabled. */
	TEST_ASSERT(pd_timer_is_disabled(port, 0));
	for (bit = 1; bit < 5; ++bit)
		TEST_ASSERT(!pd_timer_is_disabled(port, bit));
	for (; bit < PD_TIMER_COUNT; ++bit)
		TEST_ASSERT(pd_timer_is_disabled(port, bit));

	/*
	 * Disable the PE and DPM timer ranges, which contain the previously
	 * enabled timers 1-5.
	 */
	pd_timer_disable_range(port, DPM_TIMER_RANGE);
	pd_timer_disable_range(port, PE_TIMER_RANGE);
	/* Verify all timers are disabled. */
	for (bit = 0; bit < PD_TIMER_COUNT; ++bit)
		TEST_ASSERT(pd_timer_is_disabled(port, bit));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_pd_timers_init);
	RUN_TEST(test_pd_timers_bit_ops);
	RUN_TEST(test_pd_timers);

	test_print_result();
}
