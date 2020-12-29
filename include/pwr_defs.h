/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PWR_DEFS_H
#define __CROS_EC_PWR_DEFS_H

#include "system.h"

struct pwr_con_t {
	uint16_t volts;
	uint16_t milli_amps;
};

/*
 * Return power (in milliwatts) corresponding to input power connection
 * struct entry.
 */
inline int pwr_con_to_milliwatts(struct pwr_con_t *pwr)
{
	return (pwr->volts * pwr->milli_amps);
}

#endif
