/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log_core.h>

#include <pw_log_tokenized/config.h>
#include <pw_log_tokenized/handler.h>
#include <pw_log_tokenized/metadata.h>
#include <pw_span/span.h>
#include <pw_sync/interrupt_spin_lock.h>
#include <pw_tokenizer/base64.h>

extern "C" {
#include "zephyr_console_shim.h"
}

namespace pw::log_zephyr
{
namespace
{

	// The Zephyr console may output raw text along with Base64 tokenized
	// messages, which could interfere with detokenization. Output a
	// character to mark the end of a Base64 message.
	constexpr char kEndDelimiter = '\n';

	sync::InterruptSpinLock log_encode_lock;

} // namespace

extern "C" void pw_log_tokenized_HandleLog(uint32_t metadata,
					   const uint8_t log_buffer[],
					   size_t size_bytes)
{
	pw::log_tokenized::Metadata meta(metadata);

	// Encode the tokenized message as Base64.
	InlineBasicString base64_string = tokenizer::PrefixedBase64Encode<
		log_tokenized::kEncodingBufferSizeBytes>(
		span(log_buffer, size_bytes));

	if (base64_string.empty()) {
		return;
	}

	// TODO(asemjonovs):
	// https://github.com/zephyrproject-rtos/zephyr/issues/59454 Zephyr
	// frontend should protect messages from getting corrupted from multiple
	// threads.
	log_encode_lock.lock();

	base64_string += kEndDelimiter;
	console_buf_notify_chars(base64_string.c_str(), base64_string.size());

	// _is_raw is set to 0 here because the print string is required to be a
	// string literal if _is_raw is set to 1.
	Z_LOG_PRINTK(/*_is_raw=*/0, "%s", base64_string.c_str());
	log_encode_lock.unlock();
}

} // namespace pw::log_zephyr
