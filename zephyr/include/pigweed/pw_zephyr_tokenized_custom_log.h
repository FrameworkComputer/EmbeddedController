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

#endif /* __PW_ZEPHYR_TOKENIZED_CUSTOM_LOG_H */
