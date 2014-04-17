/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* GPIO UART debug printf */

#ifndef __BOARD_KEYBORG_DEBUG_H
#define __BOARD_KEYBORG_DEBUG_H

#ifdef CONFIG_DEBUG_PRINTF
void debug_printf(const char *format, ...);
#else
#define debug_printf(...)
#endif

#endif /* __BOARD_KEYBORG_DEBUG_H */
