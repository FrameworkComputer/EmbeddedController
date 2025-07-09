/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DT_BINDINGS_VIVALDI_KBD_H_
#define DT_BINDINGS_VIVALDI_KBD_H_

#include <zephyr/dt-bindings/dt-util.h>

#include <dt-bindings/kbd.h>

#define VIVALDI_TK_ABSENT 0
#define VIVALDI_TK_BACK 1
#define VIVALDI_TK_FORWARD 2
#define VIVALDI_TK_REFRESH 3
#define VIVALDI_TK_FULLSCREEN 4
#define VIVALDI_TK_OVERVIEW 5
#define VIVALDI_TK_BRIGHTNESS_DOWN 6
#define VIVALDI_TK_BRIGHTNESS_UP 7
#define VIVALDI_TK_VOL_MUTE 8
#define VIVALDI_TK_VOL_DOWN 9
#define VIVALDI_TK_VOL_UP 10
#define VIVALDI_TK_SNAPSHOT 11
#define VIVALDI_TK_PRIVACY_SCRN_TOGGLE 12
#define VIVALDI_TK_KBD_BKLIGHT_DOWN 13
#define VIVALDI_TK_KBD_BKLIGHT_UP 14
#define VIVALDI_TK_PLAY_PAUSE 15
#define VIVALDI_TK_NEXT_TRACK 16
#define VIVALDI_TK_PREV_TRACK 17
#define VIVALDI_TK_KBD_BKLIGHT_TOGGLE 18
#define VIVALDI_TK_MICMUTE 19
#define VIVALDI_TK_MENU 20
#define VIVALDI_TK_DICTATE 21
#define VIVALDI_TK_ACCESSIBILITY 22
#define VIVALDI_TK_DONOTDISTURB 23

#define VIVALDI_KEYBD_CAP_FUNCTION_KEYS BIT(0)
#define VIVALDI_KEYBD_CAP_NUMERIC_KEYPAD BIT(1)
#define VIVALDI_KEYBD_CAP_SCRNLOCK_KEY BIT(2)
#define VIVALDI_KEYBD_CAP_ASSISTANT_KEY BIT(3)

/* Strauss helper macros */

/** Strauss mapping for 14 function keys */
#define VIVALDI_KEYS_STRAUSS_T14 \
	KBD_RC(0, 2) /* T1 */    \
	KBD_RC(3, 2) /* T2 */    \
	KBD_RC(2, 2) /* T3 */    \
	KBD_RC(1, 2) /* T4 */    \
	KBD_RC(4, 4) /* T5 */    \
	KBD_RC(2, 4) /* T6 */    \
	KBD_RC(1, 4) /* T7 */    \
	KBD_RC(2, 11) /* T8 */   \
	KBD_RC(1, 9) /* T9 */    \
	KBD_RC(0, 4) /* T10 */   \
	KBD_RC(0, 1) /* T11 */   \
	KBD_RC(1, 5) /* T12 */   \
	KBD_RC(3, 5) /* T13 */   \
	KBD_RC(0, 11) /* T14 */

/** Strauss mapping for 15 function keys */
#define VIVALDI_KEYS_STRAUSS_T15 \
	VIVALDI_KEYS_STRAUSS_T14 \
	KBD_RC(0, 12) /* T15 */

#endif /* DT_BINDINGS_VIVALDI_KBD_H_ */
