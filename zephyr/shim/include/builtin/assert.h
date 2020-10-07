/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ASSERT_H
#define __CROS_EC_ASSERT_H

#include <sys/__assert.h>

#undef ASSERT
#define ASSERT __ASSERT_NO_MSG
#define assert __ASSERT_NO_MSG

#endif /* __CROS_EC_ASSERT_H */
