/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* External power via USB on Spring for Chrome EC */

#ifndef __CROS_EC_EXTPOWER_SPRING_H
#define __CROS_EC_EXTPOWER_SPRING_H

#include "common.h"

/*
 * TODO(crosbug.com/p/23813): this currently piggy-backs on the charger task.
 * Should be able to move updates to deferred functions and get rid of all the
 * ifdef's in the charger task.  At that point, all these APIs will be internal
 * to the extpower module and this entire header file can go away.
 */

/**
 * Properly limit input power on EC boot.
 *
 * Called from charger task.
 */
void extpower_charge_init(void);

/**
 * Update external power state.
 *
 * Called from charger task.
 */
void extpower_charge_update(int force_update);

/**
 * Return non-zero if external power needs update from charge task.
 */
int extpower_charge_needs_update(void);

#endif  /* __CROS_EC_EXTPOWER_SPRING_H */
