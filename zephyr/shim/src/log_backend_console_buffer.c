/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>

static uint8_t
	char_out_buf[CONFIG_PLATFORM_EC_LOG_BACKEND_CONSOLE_BUFFER_TMP_BUF_SIZE];

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	/*
	 * console_buf_notify_chars uses a mutex, which may not be
	 * locked in ISRs.
	 */
	if (k_is_in_isr())
		return 0;
	return console_buf_notify_chars(data, length);
}
LOG_OUTPUT_DEFINE(log_output_console_buffer, char_out, char_out_buf,
		  sizeof(char_out_buf));

static void process(const struct log_backend *const backend,
		    union log_msg_generic *msg)
{
	uint32_t flags = log_backend_std_get_flags();

	if (IS_ENABLED(CONFIG_PLATFORM_EC_LOG_BACKEND_CONSOLE_BUFFER_REDUCED)) {
		flags = (flags & ~LOG_OUTPUT_FLAG_LEVEL) |
			LOG_OUTPUT_FLAG_SKIP_SOURCE;
	}

	log_format_func_t log_output_func =
		log_format_func_t_get(LOG_OUTPUT_TEXT);
	log_output_func(&log_output_console_buffer, &msg->log, flags);
}

static void panic(struct log_backend const *const backend)
{
	log_backend_std_panic(&log_output_console_buffer);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	log_backend_std_dropped(&log_output_console_buffer, cnt);
}

const struct log_backend_api log_backend_console_buffer_api = {
	.process = process,
	.panic = panic,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_DEFERRED) ? dropped : NULL,
	/* TODO(b/244170593): Support switching output formats */
	.format_set = NULL,
};

LOG_BACKEND_DEFINE(log_backend_console_buffer, log_backend_console_buffer_api,
		   true);
