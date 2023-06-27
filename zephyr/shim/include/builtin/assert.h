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

/* TODO(b/269175417): This should be handled in Zephyr __assert.h */
#ifndef __ASSERT_UNREACHABLE
#define __ASSERT_UNREACHABLE CODE_UNREACHABLE
#endif

#endif /* __CROS_EC_ASSERT_H */
