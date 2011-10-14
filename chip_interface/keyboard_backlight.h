/* keyboard_backlight.h - Keyboard backlight
 *
 * (Chromium license) */

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
EcError CrKeyboardBacklightInit(uint16_t init_lightness);

/* Set the mapped PWM value */
EcError CrKeyboardBacklightSet(uint16_t lightness);

#endif  /* __CHIP_INTERFACE_KEYBOARD_BACKLIGHT_H */
