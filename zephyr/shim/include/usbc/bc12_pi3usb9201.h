/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/bc12/pi3usb9201_public.h"

#define PI3USB9201_COMPAT pericom_pi3usb9201

#define BC12_CHIP_PI3USB9201(id) { .drv = &pi3usb9201_drv, },
