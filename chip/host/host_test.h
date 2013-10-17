/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Unit testing for Chrome EC */

#ifndef __CROS_EC_HOST_TEST_H
#define __CROS_EC_HOST_TEST_H

/* Emulator exit codes */
#define EXIT_CODE_HIBERNATE (1 << 7)

/* Get emulator executable name */
const char *__get_prog_name(void);

#endif  /* __CROS_EC_HOST_TEST_H */
