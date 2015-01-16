/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ILS29035 light sensor driver
 */

#ifndef __CROS_EC_ALS_ILS29035_H
#define __CROS_EC_ALS_ILS29035_H

int isl29035_read_lux(int *lux, int af);

#endif	/* __CROS_EC_ALS_ILS29035_H */
