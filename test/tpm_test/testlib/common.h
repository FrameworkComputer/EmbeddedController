/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_TEST_TPM_TEST_TESTLIB_COMMON_H
#define __EC_TEST_TPM_TEST_TESTLIB_COMMON_H

#include "dcrypto.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/param.h>

void rand_bytes(void *buf, size_t num);

#endif  /* ! __EC_TEST_TPM_TEST_TESTLIB_COMMON_H */

