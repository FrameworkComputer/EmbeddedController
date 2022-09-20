/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include "host_command.h"
#include "test/drivers/test_state.h"
#include "timer.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_HWTIMER_64BIT),
	     "Tests expect the 64-bit HW timer");

/**
 * @brief Sets the timestamp returned by the system_get_jump_tag mock. Set to
 *        UNIT64_MAX to make the mock return NULL.
 */
static uint64_t jump_tag_time;

/* Sets the initial timer value */
FAKE_VALUE_FUNC(int, __hw_clock_source_init64, uint64_t);

FAKE_VALUE_FUNC(uint8_t *, system_get_jump_tag, uint16_t, int *, int *);

ZTEST(timer, init_from_jump_tag)
{
	/* When initializing after a system jump, the timer should get set to
	 * the time before the jump (stored in a jump tag)
	 */

	/* Set up mock to return this time */
	jump_tag_time = 0x0123456789abcdef;

	timer_init();

	zassert_equal(1, system_get_jump_tag_fake.call_count, NULL);
	zassert_equal(1, __hw_clock_source_init64_fake.call_count, NULL);
	zassert_equal(jump_tag_time,
		      __hw_clock_source_init64_fake.arg0_history[0], NULL);
}

ZTEST(timer, init_from_zero)
{
	/* When there is no jump tag, the timer should initialize to zero. */

	/* Simulate no jump tag stored */
	jump_tag_time = UINT64_MAX;

	timer_init();

	zassert_equal(1, system_get_jump_tag_fake.call_count, NULL);
	zassert_equal(1, __hw_clock_source_init64_fake.call_count, NULL);
	zassert_equal(0, __hw_clock_source_init64_fake.arg0_history[0], NULL);
}

/**
 * @brief Custom fake for system_get_jump_tag
 *
 * @param tag Which jump tag to retrieve, ignored in this application.
 * @param version Output param to write jump tag version
 * @param size Output param to write tag size
 * @return uint8_t* Pointer to tag data
 */
static uint8_t *system_get_jump_tag_custom_fake(uint16_t tag, int *version,
						int *size)
{
	ARG_UNUSED(tag);

	/* Pretend the tag doesn't exist if set to this value */
	if (jump_tag_time == UINT64_MAX) {
		return NULL;
	}

	if (version)
		*version = 1;
	if (size)
		*size = sizeof(jump_tag_time);
	return (uint8_t *)&jump_tag_time;
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	RESET_FAKE(__hw_clock_source_init64);
	RESET_FAKE(system_get_jump_tag)
	system_get_jump_tag_fake.custom_fake = system_get_jump_tag_custom_fake;
}

ZTEST_SUITE(timer, drivers_predicate_post_main, NULL, reset, reset, NULL);
