/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/tc_util.h>
#include <zephyr/ztest.h>

static uint8_t mock_output_buffer[512];
static uint32_t mock_output_len;
static uint8_t log_output_buf[4];

static int mock_output_func(uint8_t *buf, size_t size, void *ctx)
{
	memcpy(&mock_output_buffer[mock_output_len], buf, size);
	mock_output_len += size;

	return size;
}

LOG_OUTPUT_DEFINE(log_output, mock_output_func, log_output_buf,
		  sizeof(log_output_buf));

ZTEST(logging, test_ec_timestamp)
{
	const char exp_str[] = "[42.123456] test\r\n";
	char package[256] __aligned(sizeof(void *));
	int err;

	err = cbprintf_package(package, sizeof(package), 0, "test");
	zassert_true(err > 0);

	log_output_process(&log_output, 42 * USEC_PER_SEC + 123456, NULL, NULL,
			   NULL, LOG_LEVEL_INF, package, NULL, 0,
			   LOG_OUTPUT_FLAG_TIMESTAMP);

	mock_output_buffer[mock_output_len] = '\0';
	zassert_mem_equal(mock_output_buffer, exp_str, sizeof(exp_str));
}

static void logging_before(void *f)
{
	mock_output_len = 0U;
	memset(mock_output_buffer, 0, sizeof(mock_output_buffer));
}

ZTEST_SUITE(logging, NULL, NULL, logging_before, NULL, NULL);
