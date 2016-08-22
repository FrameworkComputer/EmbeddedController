/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_RBOX_H
#define __CROS_RBOX_H

/*
 * RBOX may be used to hold the EC in reset. Returns 1 if RBOX_ASSERT_EC_RST is
 * being used to hold the EC in reset and 0 if it isn't.
 */
int rbox_is_asserting_ec_reset(void);
#endif  /* __CROS_RBOX_H */
