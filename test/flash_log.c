/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test Cr-50 Non-Voltatile memory module
 */

#include <stdlib.h>

#include "common.h"
#include "flash_log.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

struct log_stats {
	size_t total_size;
	size_t entry_count;
};

static int verify_single_entry(uint8_t fill_byte, int expected_type)
{
	int entry_size;
	union entry_u e;
	size_t i;
	uint8_t *log_base = (void *)CONFIG_FLASH_LOG_BASE;

	memset(log_base, fill_byte, CONFIG_FLASH_LOG_SPACE);
	last_used_timestamp = 0;
	flash_log_init();

	/* After initialization there should be a single log entry. */
	entry_size = flash_log_dequeue_event(0, e.entry, sizeof(e.entry));
	TEST_ASSERT(entry_size == sizeof(e.r));
	TEST_ASSERT(e.r.type == expected_type);

	entry_size = flash_log_dequeue_event(e.r.timestamp, e.entry,
					     sizeof(e.entry));
	TEST_ASSERT(entry_size == 0);

	/* Verify proper entry padding. */
	i = sizeof(e.r);
	TEST_ASSERT(i % CONFIG_FLASH_WRITE_SIZE);
	for (; i % CONFIG_FLASH_WRITE_SIZE; i++)
		TEST_ASSERT(log_base[i] == FE_LOG_PAD);

	TEST_ASSERT(log_base[i] == 0xff); /* First byte above padding. */

	return EC_SUCCESS;
}

static int test_init_from_scratch(void)
{
	return verify_single_entry(0xff, FE_LOG_START);
}

static int test_init_from_corrupted(void)
{
	/* Let's mess up the log space. */
	return verify_single_entry(0x55, FE_LOG_CORRUPTED);
}

static int verify_log(struct log_stats *stats)
{
	union entry_u e;
	size_t actual_size;
	size_t actual_count;
	int entry_size;

	e.r.timestamp = 0;
	actual_size = 0;
	actual_count = 0;

	while ((entry_size = flash_log_dequeue_event(e.r.timestamp, e.entry,
						     sizeof(e))) > 0) {
		actual_count++;
		actual_size += FLASH_LOG_ENTRY_SIZE(e.r.size);
	}

	TEST_ASSERT(entry_size == 0);

	stats->total_size = actual_size;
	stats->entry_count = actual_count;

	return EC_SUCCESS;
}

static int fill_to_threshold(size_t threshold, struct log_stats *stats)
{
	int i;
	uint8_t entry_type;
	uint8_t payload_size;
	uint8_t p[MAX_FLASH_LOG_PAYLOAD_SIZE];
	size_t total_size;
	size_t entry_count;

	/* Start with an only entry in the log. */
	TEST_ASSERT(verify_single_entry(0xff, FE_LOG_START) == EC_SUCCESS);

	srand(0); /* Let's make sure it is consistent. */
	entry_count = 1;
	total_size = FLASH_LOG_ENTRY_SIZE(0);

	/* Let's fill up the log to compaction limit. */
	do {
		entry_type = rand() % 0xfe;
		payload_size = rand() % MAX_FLASH_LOG_PAYLOAD_SIZE;
		for (i = 0; i < payload_size; i++)
			p[i] = (i + entry_type) & 0xff;

		flash_log_add_event(entry_type, payload_size, p);
		total_size += FLASH_LOG_ENTRY_SIZE(payload_size);
		entry_count++;
	} while (total_size <= threshold);

	TEST_ASSERT(verify_log(stats) == EC_SUCCESS);
	TEST_ASSERT(stats->total_size == total_size);
	TEST_ASSERT(stats->entry_count == entry_count);

	/* This should get the log over the compaction threshold. */
	flash_log_add_event(entry_type, payload_size, p);
	TEST_ASSERT(verify_log(stats) == EC_SUCCESS);

	return EC_SUCCESS;
}

static int test_run_time_compaction(void)
{
	struct log_stats stats;

	TEST_ASSERT(fill_to_threshold(RUN_TIME_LOG_FULL_WATERMARK, &stats) ==
		    EC_SUCCESS);

	/*
	 * Compacted space is guaranteed not to exceed the threshold plus the
	 * size of the largest possible entry.
	 */
	TEST_ASSERT(stats.total_size <
		    (COMPACTION_SPACE_PRESERVE +
		     FLASH_LOG_ENTRY_SIZE(MAX_FLASH_LOG_PAYLOAD_SIZE)));

	return EC_SUCCESS;
}

static int test_init_time_compaction(void)
{
	struct log_stats stats;

	TEST_ASSERT(fill_to_threshold(STARTUP_LOG_FULL_WATERMARK, &stats) ==
		    EC_SUCCESS);

	/*
	 * Init should roll the log back below the compaction preservation
	 * threshold.
	 */
	flash_log_init();
	TEST_ASSERT(verify_log(&stats) == EC_SUCCESS);

	/*
	 * Compacted space is guaranteed not to exceed the threshold plus the
	 * size of the largest possible entry.
	 */
	TEST_ASSERT(stats.total_size <
		    (COMPACTION_SPACE_PRESERVE +
		     FLASH_LOG_ENTRY_SIZE(MAX_FLASH_LOG_PAYLOAD_SIZE)));

	return EC_SUCCESS;
}

static int test_lock_failure_reporting(void)
{
	union entry_u e;

	TEST_ASSERT(test_init_from_scratch() == EC_SUCCESS);
	lock_failures_count = 0;
	log_event_in_progress = 1;

	/* This should fail. */
	flash_log_add_event(FE_LOG_TEST, 0, NULL);

	/* Lock count should have been incremented. */
	TEST_ASSERT(lock_failures_count == 1);

	/* This should also fail. */
	TEST_ASSERT(flash_log_dequeue_event(0, e.entry, sizeof(e.entry)) ==
		    -EC_ERROR_BUSY);

	log_event_in_progress = 0;
	/* This should succeed. */
	flash_log_add_event(FE_LOG_TEST, 0, NULL);

	TEST_ASSERT(lock_failures_count == 0);

	/* There should be three entries in the log now. */
	flash_log_dequeue_event(0, e.entry, sizeof(e.entry));
	TEST_ASSERT(e.r.type == FE_LOG_START);

	flash_log_dequeue_event(e.r.timestamp, e.entry, sizeof(e.entry));
	TEST_ASSERT(e.r.type == FE_LOG_LOCKS);
	TEST_ASSERT(FLASH_LOG_PAYLOAD_SIZE(e.r.size) == 1);
	TEST_ASSERT(e.r.payload[0] == 1);

	flash_log_dequeue_event(e.r.timestamp, e.entry, sizeof(e.entry));
	TEST_ASSERT(e.r.type == FE_LOG_TEST);

	return EC_SUCCESS;
}

static int test_setting_base_timestamp(void)
{
	union entry_u eu;
	uint32_t saved_stamp;
	timestamp_t ts;
	uint32_t delta_time;
	/* Value collected on May 13 2019 */
	uint32_t recent_seconds_since_epoch = 1557793625;

	ts.val = 0;
	force_time(ts);
	TEST_ASSERT(verify_single_entry(0xff, FE_LOG_START) == EC_SUCCESS);
	TEST_ASSERT(flash_log_dequeue_event(0, eu.entry, sizeof(eu)) > 0);

	saved_stamp = eu.r.timestamp;

	/* Let the next log timestamp be 1000 s later. */
	delta_time = 1000;

	/*
	 * Move internal clock uptime of 1000 s (convert value to microseconds
	 * first).
	 */
	ts.val = ((uint64_t)saved_stamp + delta_time) * 1000000;
	force_time(ts);

	/* Verify that the second event is within 1001 s from the first one. */
	flash_log_add_event(FE_LOG_TEST, 0, NULL);
	TEST_ASSERT(flash_log_dequeue_event(saved_stamp, eu.entry, sizeof(eu)) >
		    0);
	TEST_ASSERT((eu.r.timestamp - saved_stamp - delta_time) < 2);

	/* Set timestamp base to current time. */
	TEST_ASSERT(flash_log_set_tstamp(recent_seconds_since_epoch) ==
		    EC_SUCCESS);

	/* Create an entry with the latest timestamp. */
	flash_log_add_event(FE_LOG_TEST, 0, NULL);

	/* Verify that it has been logged with the correct timestamp. */
	TEST_ASSERT(flash_log_dequeue_event(eu.r.timestamp, eu.entry,
					    sizeof(eu)) > 0);
	TEST_ASSERT((eu.r.timestamp - recent_seconds_since_epoch) < 2);

	/* Verify that it is impossible to roll timestamps back. */
	TEST_ASSERT(flash_log_set_tstamp(recent_seconds_since_epoch - 100) ==
		    EC_ERROR_INVAL);

	/* But is possible to roll further forward. */
	TEST_ASSERT(flash_log_set_tstamp(recent_seconds_since_epoch + 100) ==
		    EC_SUCCESS);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_init_from_scratch);
	RUN_TEST(test_init_from_corrupted);
	RUN_TEST(test_run_time_compaction);
	RUN_TEST(test_init_time_compaction);
	RUN_TEST(test_lock_failure_reporting);
	RUN_TEST(test_setting_base_timestamp);

	test_print_result();
}
