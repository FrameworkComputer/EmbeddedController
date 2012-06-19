/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TPSChrome PMU APIs.
 */

#ifndef __CROS_EC_TPSCHROME_H
#define __CROS_EC_TPSCHROME_H


int pmu_is_charger_alarm(void);
int pmu_get_power_source(int *ac_good, int *battery_good);
void pmu_init(void);

#endif /* __CROS_EC_TPSCHROME_H */

