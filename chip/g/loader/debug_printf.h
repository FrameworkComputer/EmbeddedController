/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_CHIP_G_LOADER_DEBUG_PRINTF_H
#define __EC_CHIP_G_LOADER_DEBUG_PRINTF_H

void debug_printf(const char *format, ...);

#ifdef DEBUG
#define VERBOSE debug_printf
#else
#define VERBOSE(...)
#endif

#endif /* __EC_CHIP_G_LOADER_DEBUG_PRINTF_H */
