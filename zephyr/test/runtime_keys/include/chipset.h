/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

enum chipset_shutdown_reason {
	CHIPSET_RESET_KB_WARM_REBOOT,
};

void chipset_reset(enum chipset_shutdown_reason reason);
