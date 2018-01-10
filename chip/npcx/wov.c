/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#ifndef NPCX_WOV_SUPPORT
#error "Do not enable CONFIG_WAKE_ON_VOICE if npcx ec doesn't support WOV !"
#endif
