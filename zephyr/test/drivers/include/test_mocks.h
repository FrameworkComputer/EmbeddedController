/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>

/* Mocks for common/init_rom.c */
DECLARE_FAKE_VALUE_FUNC(const void *, init_rom_map, const void *, int);
DECLARE_FAKE_VOID_FUNC(init_rom_unmap, const void *, int);
DECLARE_FAKE_VALUE_FUNC(int, init_rom_copy, int, int, int);
