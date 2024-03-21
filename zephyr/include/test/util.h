/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __TEST_UTIL
#define __TEST_UTIL

#define TEST_WAIT_FOR_INTERVAL_MS 100

/**
 * @brief Wait for an expression to return true with a timeout, expression is
 * checked every TEST_WAIT_FOR_INTERVAL_MS.
 *
 * @param expr Expression to poll
 * @param timeout_ms Timeout to wait for in milliseconds
 *
 * @retval expr as a boolean return, false for timeout
 */
#define TEST_WAIT_FOR(expr, timeout_ms) \
	WAIT_FOR(expr, 1000 * (timeout_ms), k_msleep(TEST_WAIT_FOR_INTERVAL_MS))

/**
 * @brief Delay for timeout_ms in intervals of TEST_WAIT_FOR_INTERVAL_MS
 *
 * @param timeout_ms Timeout to wait for in milliseconds
 */
#define TEST_WORKING_DELAY(timeout_ms) while (TEST_WAIT_FOR(false, timeout_ms))

#endif
