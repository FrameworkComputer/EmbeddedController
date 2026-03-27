/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

int extpower_present = 0;
int extpower_is_present(void)
{
	return extpower_present;
}
