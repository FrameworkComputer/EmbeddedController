/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This defines a list of lightbar messages. It's done in this odd way so that
 * we can automatically derive the correct constants, functions, and message
 * types.
 */
#define LIGHTBAR_MSG_LIST                       \
	LBMSG(ERROR)				\
	LBMSG(S5)				\
	LBMSG(S3)				\
	LBMSG(S0)				\
	LBMSG(S5S3)				\
	LBMSG(S3S0)				\
	LBMSG(S0S3)				\
	LBMSG(S3S5)				\
	LBMSG(STOP)				\
	LBMSG(RUN)				\
	LBMSG(PULSE)				\
	LBMSG(TEST)				\
	LBMSG(KONAMI)
