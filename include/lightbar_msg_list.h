/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This defines a list of lightbar messages. It's done in this odd way so that
 * we can automatically derive the correct constants, functions, and message
 * types.
 */
#define LIGHTBAR_MSG_LIST                       \
	LBMSG(ERROR),		/* 0 */		\
	LBMSG(S5),		/* 1 */		\
	LBMSG(S3),		/* 2 */		\
	LBMSG(S0),		/* 3 */		\
	LBMSG(S5S3),		/* 4 */		\
	LBMSG(S3S0),		/* 5 */		\
	LBMSG(S0S3),		/* 6 */		\
	LBMSG(S3S5),		/* 7 */		\
	LBMSG(STOP),		/* 8 */		\
	LBMSG(RUN),		/* 9 */		\
	LBMSG(PULSE),		/* A */		\
	LBMSG(TEST),		/* B */		\
	LBMSG(KONAMI),		/* C */         \
	LBMSG(TAP),		/* D */		\
	LBMSG(PROGRAM),		/* E */
