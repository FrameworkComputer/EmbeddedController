/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

void mpu_enable(void)
{
	/*
	 * Since program is run directly from internal flash, there is nothing
	 * to do here. (no code RAM will be overwritten)
	 */
}
