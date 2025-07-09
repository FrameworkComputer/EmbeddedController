/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration */

#ifndef __KBD_GCS_H
#define __KBD_GCS_H

/*
 * KEYBOARD_COLS_MAX has the build time column size. It's used to allocate
 * exact spaces for arrays. Actual keyboard scanning is done using
 * keyboard_cols, which holds a runtime column size.
 */
#undef KEYBOARD_COLS_MAX
#undef KEYBOARD_ROWS

#define KEYBOARD_COLS_MAX 16
#define KEYBOARD_ROWS 8

#endif /* __KBD_GCS_H */
