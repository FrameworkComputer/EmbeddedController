/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_CUSTOM_SHELL_H
#define __ZEPHYR_CUSTOM_SHELL_H

#include <zephyr/sys/__assert.h>

/* If static_assert wasn't defined by zephyr/sys/__assert.h that means it's not
 * supported, just ignore it.
 */
#ifndef static_assert
#define static_assert(...)
#endif

#include <pw_log/log.h>

#undef shell_fprintf
#undef shell_info
#undef shell_print
#undef shell_warn
#undef shell_error

#define shell_fprintf(sh, color, fmt, ...)       \
	{                                        \
		(void)(sh);                      \
		(void)(color);                   \
		PW_LOG_INFO(fmt, ##__VA_ARGS__); \
	}

#define shell_info(_sh, _ft, ...)                \
	{                                        \
		(void)(_sh);                     \
		PW_LOG_INFO(_ft, ##__VA_ARGS__); \
	}

#define shell_print(_sh, _ft, ...)               \
	{                                        \
		(void)(_sh);                     \
		PW_LOG_INFO(_ft, ##__VA_ARGS__); \
	}

#define shell_warn(_sh, _ft, ...)                \
	{                                        \
		(void)(_sh);                     \
		PW_LOG_WARN(_ft, ##__VA_ARGS__); \
	}

#define shell_error(_sh, _ft, ...)                \
	{                                         \
		(void)(_sh);                      \
		PW_LOG_ERROR(_ft, ##__VA_ARGS__); \
	}

#endif /* __ZEPHYR_CUSTOM_SHELL_H */
