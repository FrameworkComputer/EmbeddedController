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

/* Bytecode field constants */
enum lb_color {
	LB_COL_RED,
	LB_COL_GREEN,
	LB_COL_BLUE,
	LB_COL_ALL
};

enum lb_control {
	LB_CONT_COLOR0,
	LB_CONT_COLOR1,
	LB_CONT_PHASE,
	LB_CONT_MAX
};

#ifdef CONFIG_ALS_LIGHTBAR_DIMMING
/*
 * For dimming the lightbar in the dark, we define an array to
 * describe the expected colors:
 * if luminosity is more than 'lux_up', the color defined will be used.
 * if luminosity is more than 'lux_down', we will look at the next band.
 * The last entry must have lux == 0.
 * Defining brightness is not enough to prevent washed color in low
 * lux setting.
 */
struct lb_brightness_def {
	uint16_t lux_up;
	uint16_t lux_down;
	struct rgb_s color[4];
};

extern const struct lb_brightness_def lb_brightness_levels[];
extern const unsigned lb_brightness_levels_count;
#endif

/* Request a preset sequence from the lightbar task. */
void lightbar_sequence_f(enum lightbar_sequence num, const char *f);
#define lightbar_sequence(A) lightbar_sequence_f(A, __func__)

/****************************************************************************/
/* External stuff */

/* These are used for demo purposes */
#define DEMO_MODE_DEFAULT 0
extern void demo_battery_level(int inc);
extern void demo_is_charging(int ischarge);
extern void demo_brightness(int inc);
extern void demo_tap(void);
#endif  /* __CROS_EC_LIGHTBAR_H */
