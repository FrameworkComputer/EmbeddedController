/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <zephyr/logging/log_core.h>
#include <zephyr/sys/__assert.h>

// If static_assert wasn't defined by zephyr/sys/__assert.h that means it's not
// supported, just ignore it.
#ifndef static_assert
#define static_assert(...)
#endif

#include <pw_log/log.h>

#undef LOG_DBG
#undef LOG_INF
#undef LOG_WRN
#undef LOG_ERR

#define Z_PW_LOG(_level, fn, format, ...)               \
	do {                                            \
		if (!Z_LOG_CONST_LEVEL_CHECK(_level)) { \
			break;                          \
		}                                       \
		fn(format "\n", ##__VA_ARGS__);         \
	} while (false)

#define LOG_DBG(format, ...) \
	Z_PW_LOG(LOG_LEVEL_DBG, PW_LOG_DEBUG, format, ##__VA_ARGS__)
#define LOG_INF(format, ...) \
	Z_PW_LOG(LOG_LEVEL_INF, PW_LOG_INFO, format, ##__VA_ARGS__)
#define LOG_WRN(format, ...) \
	Z_PW_LOG(LOG_LEVEL_WRN, PW_LOG_WARN, format, ##__VA_ARGS__)
#define LOG_ERR(format, ...) \
	Z_PW_LOG(LOG_LEVEL_ERR, PW_LOG_ERROR, format, ##__VA_ARGS__)
