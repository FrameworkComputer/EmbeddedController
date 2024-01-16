/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/dt-bindings/dt-util.h>

#define VIVALDI_RC(row, col) ((((row) & 0xff) << 8) | ((col) & 0xff))

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

#define VIVALDI_KEYBD_CAP_FUNCTION_KEYS BIT(0)
#define VIVALDI_KEYBD_CAP_NUMERIC_KEYPAD BIT(1)
#define VIVALDI_KEYBD_CAP_SCRNLOCK_KEY BIT(2)
