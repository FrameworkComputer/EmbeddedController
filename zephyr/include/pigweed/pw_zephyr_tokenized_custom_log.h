/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __PW_ZEPHYR_TOKENIZED_CUSTOM_LOG_H
#define __PW_ZEPHYR_TOKENIZED_CUSTOM_LOG_H

/* See src/third_party/pigweed/pw_log_tokenized/public/pw_log_tokenized/config.h
 * to see how file and module can be included in tokenized logging output.
 * Additional handling is needed in EC3PO to parse additional fields.
 */
#define PW_LOG_TOKENIZED_FORMAT_STRING(string) string

/* Use a rarely used char in EC logging as tokenizer prefix
 * If prefix changes, make sure to update the following files to match
 *  -- src/third_party/hdctools/servo/ec3po/console.py
 *  -- src/platform2/timberslide/token_config.h
 */
#define PW_TOKENIZER_NESTED_PREFIX_STR "`"

/* Increase PW FLAG_BITS from 2 to 6.
 * This will satisfy the uint32_t channel_mask used in
 * ec/common/console_output.c.  Although enum console_channel
 * CC_CHANNEL_COUNT may exceed 32 in certain configs.
 * An extra bit is needed as the channel is offset by 1,
 * to allow for normal Zephyr based logging that doesn't use
 * channels.
 * Borrow bits from MODULE_BITS to keep 32 bit meta data
 * as this is not used in EC logging output.
 */
#define PW_LOG_TOKENIZED_FLAG_BITS 6
#define PW_LOG_TOKENIZED_MODULE_BITS 12

#define PW_EC_CHANNEL_TO_FLAG(channel) ((channel) + 1)
#define PW_FLAG_TO_EC_CHANNEL(flag) ((enum console_channel)((flag)-1))

#endif /* __PW_ZEPHYR_TOKENIZED_CUSTOM_LOG_H */
