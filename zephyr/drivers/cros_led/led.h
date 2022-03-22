/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

/*
 * Common macros and defines used by both PWM and GPIO drivers..
 */

/*
 * Generate names or enums used to reference the LED driver structures.
 * These names are used internally to allow the policy tables to
 * reference the generated tables.
 * The names are generated from the DTS node name prepended with a string.
 */
#define LED_TYPE_INDEX(id)	DT_CAT(L_H_I_, id)

#define GEN_TYPE_INDEX_ENUM(id)	LED_TYPE_INDEX(id),
/*
 * Generate enums for the action indices for
 * each of the policy entries. These are not typed
 * as they are only used internally as an index.
 */
#define GEN_ACTION_ENUM(id)	LED_ACTION(id),

#define GEN_ACTION_ENUM_LIST(id)			\
enum {							\
	DT_FOREACH_CHILD(id, GEN_ACTION_ENUM)		\
};

#endif /* __CROS_EC_LED_H__ */
