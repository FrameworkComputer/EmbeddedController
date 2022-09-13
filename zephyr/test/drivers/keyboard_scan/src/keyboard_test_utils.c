/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <emul/emul_kb_raw.h>
#include <zephyr/drivers/emul.h>

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));

int emulate_keystate(int row, int col, int pressed)
{
	return emul_kb_raw_set_kbstate(dev, row, col, pressed);
}

void clear_emulated_keys(void)
{
	emul_kb_raw_reset(dev);
}
