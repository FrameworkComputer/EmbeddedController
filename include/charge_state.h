/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_CHARGE_STATE_H
#define __CROS_EC_CHARGE_STATE_H

#include "common.h"

/* Stuff that's common to all charger implementations can go here. */


/* Pick the right implementation */
#ifdef CONFIG_CHARGER_V1
#ifdef CONFIG_CHARGER_V2
#error "Choose either CONFIG_CHARGER_V1 or CONFIG_CHARGER_V2, not both"
#else
#include "charge_state_v1.h"
#endif
#else  /* not V1 */
#ifdef CONFIG_CHARGER_V2
#include "charge_state_v2.h"
#endif
#endif	/* CONFIG_CHARGER_V1 */

#endif	/* __CROS_EC_CHARGE_STATE_H */
