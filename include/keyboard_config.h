/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard configuration constants for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_CONFIG_H
#define __CROS_EC_KEYBOARD_CONFIG_H

#include "common.h"

#define KEYBOARD_ROW_TO_MASK(r) (1 << (r))

#ifdef CONFIG_KEYBOARD_CUSTOMIZATION
/* include the board layer keyboard header file */
#include "keyboard_customization.h"
#define KEYBOARD_COLS KEYBOARD_COLS_MAX
#else /* CONFIG_KEYBOARD_CUSTOMIZATION */

/* Keyboard matrix is 13 (or 15 with keypad) output columns x 8 input rows */
#define KEYBOARD_COLS_WITH_KEYPAD 15
#define KEYBOARD_COLS_NO_KEYPAD 13

/*
 * KEYBOARD_COLS is the column size of the default matrix. KEYBOARD_COLS_MAX
 * has the column size of the largest matrix. It's used to statically allocate
 * arrays used by the scanner. keyboard_cols holds a runtime column size. The
 * scanner uses it as a loop terminal.
 */
#ifdef CONFIG_KEYBOARD_COLS
#define KEYBOARD_COLS CONFIG_KEYBOARD_COLS
#elif defined(CONFIG_KEYBOARD_KEYPAD)
#define KEYBOARD_COLS KEYBOARD_COLS_WITH_KEYPAD
#else
#define KEYBOARD_COLS KEYBOARD_COLS_NO_KEYPAD
#endif
#ifndef KEYBOARD_COLS_MAX
#define KEYBOARD_COLS_MAX KEYBOARD_COLS
#endif
#define KEYBOARD_ROWS 8

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_KEYBOARD_MULTIPLE
/* Columns for keys we particularly care about when using a single keyboard
 * layout. */
#define KEYBOARD_COL_DOWN 11
#define KEYBOARD_ROW_DOWN 6
#define KEYBOARD_COL_ESC 1
#define KEYBOARD_ROW_ESC 1
#define KEYBOARD_COL_KEY_H 6
#define KEYBOARD_ROW_KEY_H 1
#ifdef CONFIG_KEYBOARD_STRAUSS
#define KEYBOARD_COL_KEY_R 9
#else
#define KEYBOARD_COL_KEY_R 3
#endif
#define KEYBOARD_ROW_KEY_R 7
#ifdef CONFIG_KEYBOARD_STRAUSS
#define KEYBOARD_COL_LEFT_ALT 13
#else
#define KEYBOARD_COL_LEFT_ALT 10
#endif
#define KEYBOARD_ROW_LEFT_ALT 6
#define KEYBOARD_COL_REFRESH 2
#ifdef CONFIG_KEYBOARD_REFRESH_ROW3
#define KEYBOARD_ROW_REFRESH 3
#else
#define KEYBOARD_ROW_REFRESH 2
#endif
#define KEYBOARD_COL_RIGHT_ALT 10
#define KEYBOARD_ROW_RIGHT_ALT 0
#define KEYBOARD_COL_LEFT_SHIFT 7
#define KEYBOARD_ROW_LEFT_SHIFT 5

#endif /* CONFIG_KEYBOARD_MULTIPLE */

#ifdef CONFIG_KEYBOARD_RUNTIME_KEYS
/*
 * Vol-up is part of vivaldi (a.k.a. top row keys). Thus, it needs to be
 * customized per boards. The default value is only used if
 * board_vivaldi_keybd_config() returns NULL.
 */
#ifndef KEYBOARD_DEFAULT_COL_VOL_UP
#define KEYBOARD_DEFAULT_COL_VOL_UP 4
#endif
#ifndef KEYBOARD_DEFAULT_ROW_VOL_UP
#define KEYBOARD_DEFAULT_ROW_VOL_UP 0
#endif
#endif /* CONFIG_KEYBOARD_RUNTIME_KEYS */

#endif /* CONFIG_KEYBOARD_CUSTOMIZATION */

/* Below are some safety checks for keyboard_customization.h files. */
#ifdef CONFIG_KEYBOARD_MULTIPLE
#if defined(KEYBOARD_COL_DOWN) || defined(KEYBOARD_ROW_DOWN) ||               \
	defined(KEYBOARD_COL_ESC) || defined(KEYBOARD_ROW_ESC) ||             \
	defined(KEYBOARD_COL_KEY_H) || defined(KEYBOARD_ROW_KEY_H) ||         \
	defined(KEYBOARD_COL_KEY_R) || defined(KEYBOARD_ROW_KEY_R) ||         \
	defined(KEYBOARD_COL_LEFT_ALT) || defined(KEYBOARD_ROW_LEFT_ALT) ||   \
	defined(KEYBOARD_COL_REFRESH) || defined(KEYBOARD_ROW_REFRESH) ||     \
	defined(KEYBOARD_COL_RIGHT_ALT) || defined(KEYBOARD_ROW_RIGHT_ALT) || \
	defined(KEYBOARD_COL_LEFT_SHIFT) || defined(KEYBOARD_ROW_LEFT_SHIFT)
#error Populate the structs key_typ & boot_key_list instead of keyboard macros.
#endif
#endif /* CONFIG_KEYBOARD_MULTIPLE */
#ifndef CONFIG_KEYBOARD_RUNTIME_KEYS
#if defined(KEYBOARD_DEFAULT_COL_VOL_UP) || defined(KEYBOARD_DEFAULT_ROW_VOL_UP)
#error KEYBOARD_DEFAULT_{ROW,COL}_VOL_UP are only used with \
CONFIG_KEYBOARD_RUNTIME_KEYS.
#endif
#endif /* CONFIG_KEYBOARD_RUNTIME_KEYS */
/*
 * GSC may not mask pass-through rows even when the power button is pressed.
 * Keyscan will clear the corresponding rows.
 */
#ifdef CONFIG_KSI0_NOT_MASKED_BY_GSC
#define KEYBOARD_MASKED_BY_POWERBTN(row_refresh) \
	KEYBOARD_ROW_TO_MASK(row_refresh)
#else
#define KEYBOARD_MASKED_BY_POWERBTN(row_refresh) \
	(KEYBOARD_ROW_TO_MASK(row_refresh) | KEYBOARD_ROW_TO_MASK(0))
#endif /* CONFIG_KSI0_NOT_MASKED_BY_GSC */

/* For safety, after any ec_response_keybd_config, add this assert. */
#define BUILD_ASSERT_REFRESH_RC(row, col) \
	BUILD_ASSERT(KEYBOARD_ROW_REFRESH == row && KEYBOARD_COL_REFRESH == col)
#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KEYBOARD_CONFIG_H */
