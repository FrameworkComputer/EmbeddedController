/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Keyboard configuration */

#ifndef __KEYBOARD_CUSTOMIZATION_H
#define __KEYBOARD_CUSTOMIZATION_H

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_STRAUSS
#include "kbd_strauss.h"
#elif defined(CONFIG_BOARD_PTLGCS)
#include "kbd_gcs.h"
#endif

#endif /* __KEYBOARD_CUSTOMIZATION_H */
