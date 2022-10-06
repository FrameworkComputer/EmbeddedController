/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppc/aoz1380_public.h"

#define AOZ1380_COMPAT aoz_aoz1380

/* Note: This chip has no i2c interface */
#define PPC_CHIP_AOZ1380(id) { .drv = &aoz1380_drv },
