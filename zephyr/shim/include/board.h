/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

#include <devicetree.h>

/* Included shimed version of gpio signal. */
#include <gpio_signal.h>
/* Include board specific gpio mapping/aliases if named_gpios node exists */
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
#include <gpio_map.h>
#endif

/* Once SHIMMED_TASKS is enabled, must provide a shimmed_tasks header */
#ifdef CONFIG_SHIMMED_TASKS
#include "shimmed_tasks.h"
#endif

#endif  /* __BOARD_H */
