/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul_stub_device.h>

/* The Zephyr model only initializes an emulator as part of initializing the
 * "real" driver.  For the eSPI host emulator, there isn't a real driver,
 * the zephyr,espi-emul-espi-host node is modeled as a child of the
 * peripheral eSPI bus.
 * Stub out a driver to pair with the eSPI host emulator.
 */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
