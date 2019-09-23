/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Synchronous UART debug printf */

#ifndef __CROS_EC_DEBUG_H
#define __CROS_EC_DEBUG_H

#ifdef CONFIG_DEBUG_PRINTF
__attribute__((__format__(__printf__, 1, 2)))
void debug_printf(const char *format, ...);
#else
#define debug_printf(...)
#endif

#endif /* __CROS_EC_DEBUG_H */
