/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Functions needed by smart battery driver.
 */

#ifndef __CROS_EC_SMART_BATTERY_STUB_H
#define __CROS_EC_SMART_BATTERY_STUB_H

/* Read from charger */
int sbc_read(int cmd, int *param);

/* Write to charger */
int sbc_write(int cmd, int param);

/* Read from battery */
int sb_read(int cmd, int *param);

/* Write to battery */
int sb_write(int cmd, int param);

#endif  /* __CROS_EC_SMART_BATTERY_STUB_H */
