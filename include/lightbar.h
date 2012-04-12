/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ask the EC to set the lightbar state to reflect the CPU activity */

#ifndef __CROS_EC_LIGHTBAR_H
#define __CROS_EC_LIGHTBAR_H

enum lightbar_sequence {
	LIGHTBAR_NULL = 0,			/* not used */
	/* CPU states */
	LIGHTBAR_S5,				/* 1 */
	LIGHTBAR_S3,				/* 2 */
	LIGHTBAR_S0,				/* 3 */
	/* CPU state transitions */
	LIGHTBAR_S5S3,				/* 4 */
	LIGHTBAR_S3S0,				/* 5 */
	LIGHTBAR_S0S3,				/* 6 */
	LIGHTBAR_S3S5,				/* 7 */
	/* Who's in charge?  */
	LIGHTBAR_EC_STOP,			/* 8 - EC ignore LEDs */
	LIGHTBAR_EC_RUN,			/* 9 - EC drive LEDs */
	/* Extra patterns */
	LIGHTBAR_PULSE,				/* a */
	LIGHTBAR_TEST,				/* b */
	LIGHTBAR_KONAMI,			/* c */
	/* that's all */
	LIGHTBAR_NUM_SEQUENCES
};

/* Request a preset sequence from the lightbar task. */
void lightbar_sequence(enum lightbar_sequence s);

#endif  /* __CROS_EC_LIGHTBAR_H */
