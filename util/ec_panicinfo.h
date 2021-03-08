/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EC_PANICINFO_H
#define EC_PANICINFO_H

#include "panic.h"

/**
 * Prints panic information to stdout.
 *
 * @param pdata  Panic information to print
 * @return 0 if success or non-zero error code if error.
 */
int parse_panic_info(const char *data, size_t size);

#endif /* EC_PANICINFO_H */
