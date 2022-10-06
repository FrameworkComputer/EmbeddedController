/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ASSERT_H
#define __CROS_EC_ASSERT_H

#include <zephyr/sys/__assert.h>

#undef ASSERT
#undef assert
#define ASSERT __ASSERT_NO_MSG
#define assert __ASSERT_NO_MSG

#endif /* __CROS_EC_ASSERT_H */
