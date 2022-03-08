/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"

/*
 * There are some collisions between the old chipset API and
 * the new API.
 */
void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}
