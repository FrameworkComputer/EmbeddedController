/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ROHM BD99992GW PMIC register map.
 */

#ifndef __CROS_EC_PMIC_BD99992GW_H
#define __CROS_EC_PMIC_BD99992GW_H

#include "temp_sensor/bd99992gw.h"

#define BD99992GW_REG_SDWNCTRL		0x49
#define BD99992GW_SDWNCTRL_SWDN		(1 << 0) /* SWDN mask */

#endif  /* __CROS_EC_PMIC_BD99992GW_H */
