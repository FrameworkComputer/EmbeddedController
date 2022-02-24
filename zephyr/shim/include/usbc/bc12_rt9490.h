/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/rt9490.h"

#define RT9490_BC12_COMPAT richtek_rt9490_bc12

#define BC12_CHIP_RT9490(id) { .drv = &rt9490_bc12_drv, },
