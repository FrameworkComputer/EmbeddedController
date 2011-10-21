/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * keyboard_backlight.h - Keyboard backlight
 */

#ifndef __CHIP_INTERFACE_KEYBOARD_BACKLIGHT_H
#define __CHIP_INTERFACE_KEYBOARD_BACKLIGHT_H

/*
 * The lightness value in this interface:
 *
 *       0 - off
 *     255 - full
 *  others - undefined
 */

/* Configure PWM port and set the initial backlight value. */
EcError EcKeyboardBacklightInit(uint16_t init_lightness);

/* Set the mapped PWM value */
EcError EcKeyboardBacklightSet(uint16_t lightness);

#endif  /* __CHIP_INTERFACE_KEYBOARD_BACKLIGHT_H */
