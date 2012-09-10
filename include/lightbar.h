/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Ask the EC to set the lightbar state to reflect the CPU activity */

#ifndef __CROS_EC_LIGHTBAR_H
#define __CROS_EC_LIGHTBAR_H

/****************************************************************************/
/* Internal stuff */

/* Define the types of sequences */
#define LBMSG(state) LIGHTBAR_##state
#include "lightbar_msg_list.h"
enum lightbar_sequence {
	LIGHTBAR_MSG_LIST
	LIGHTBAR_NUM_SEQUENCES
};
#undef LBMSG

/* Request a preset sequence from the lightbar task. */
void lightbar_sequence(enum lightbar_sequence s);

/****************************************************************************/
/* External stuff */

/* These are used for demo purposes */
extern void demo_battery_level(int inc);
extern void demo_is_charging(int ischarge);
extern void demo_brightness(int inc);

#endif  /* __CROS_EC_LIGHTBAR_H */
