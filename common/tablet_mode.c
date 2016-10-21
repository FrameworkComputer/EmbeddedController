/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Return 1 if in tablet mode, 0 otherwise */
static int tablet_mode = 1;

int tablet_get_mode(void)
{
	return tablet_mode;
}

void tablet_set_mode(int mode)
{
	tablet_mode = mode;
}

