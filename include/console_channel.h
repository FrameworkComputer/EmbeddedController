/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console channels */
#ifdef __cplusplus
enum console_channel : int {
#else
enum console_channel {
#endif
#define CONSOLE_CHANNEL(enumeration, string) enumeration,
#include "console_channel.inc"
#undef CONSOLE_CHANNEL

	/* Channel count; not itself a channel */
	CC_CHANNEL_COUNT
};
