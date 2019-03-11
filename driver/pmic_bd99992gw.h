/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ROHM BD99992GW PMIC register map.
 */

#ifndef __CROS_EC_PMIC_BD99992GW_H
#define __CROS_EC_PMIC_BD99992GW_H

#include "temp_sensor/bd99992gw.h"

#define BD99992GW_REG_PWRSRCINT		0x04
#define BD99992GW_REG_RESETIRQ1		0x08
#define BD99992GW_REG_PBCONFIG		0x14
#define BD99992GW_REG_PWRSTAT1		0x16
#define BD99992GW_REG_PWRSTAT2		0x17
#define BD99992GW_REG_VCCIOCNT		0x30
#define BD99992GW_REG_V5ADS3CNT		0x31
#define BD99992GW_REG_V18ACNT		0x34
#define BD99992GW_REG_V100ACNT		0x37
#define BD99992GW_REG_V085ACNT		0x38
#define BD99992GW_REG_VRMODECTRL	0x3b
#define BD99992GW_REG_DISCHGCNT1	0x3c
#define BD99992GW_REG_DISCHGCNT2	0x3d
#define BD99992GW_REG_DISCHGCNT3	0x3e
#define BD99992GW_REG_DISCHGCNT4	0x3f
#define BD99992GW_REG_SDWNCTRL		0x49
#define BD99992GW_SDWNCTRL_SWDN		BIT(0) /* SWDN mask */

#endif  /* __CROS_EC_PMIC_BD99992GW_H */
