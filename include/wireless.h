/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wireless API for Chrome EC */

#ifndef __CROS_EC_WIRELESS_H
#define __CROS_EC_WIRELESS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wireless power state for wireless_set_state() */
enum wireless_power_state { WIRELESS_OFF, WIRELESS_SUSPEND, WIRELESS_ON };

/**
 * Set wireless power state.
 */
#ifdef CONFIG_WIRELESS
void wireless_set_state(enum wireless_power_state state);
#else
static inline void wireless_set_state(enum wireless_power_state state)
{
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_WIRELESS_H */
