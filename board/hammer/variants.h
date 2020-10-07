/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Hammer variants configuration, there should be no/little BOARD_ checks
 * in the rest of the files. If this grows out of control, we can create
 * variant_*.h files.
 */

#ifndef __CROS_EC_VARIANTS_H
#define __CROS_EC_VARIANTS_H

/* USB ID */
#ifdef BOARD_HAMMER
#define CONFIG_USB_PID 0x5022
#elif defined(BOARD_MAGNEMITE)
#define CONFIG_USB_PID 0x503d
#elif defined(BOARD_MASTERBALL)
#define CONFIG_USB_PID 0x503c
#elif defined(BOARD_MOONBALL)
#define CONFIG_USB_PID 0x5044
#elif defined(BOARD_STAFF)
#define CONFIG_USB_PID 0x502b
#elif defined(BOARD_WAND)
#define CONFIG_USB_PID 0x502d
#elif defined(BOARD_WHISKERS)
#define CONFIG_USB_PID 0x5030
#elif defined(BOARD_ZED)
/* TODO: update PID */
#define CONFIG_USB_PID 0x5022
#else
#error "Invalid board"
#endif

#ifdef SECTION_IS_RW

/* Touchpad interface, firmware size and physical dimension. */
#if defined(BOARD_HAMMER) || defined(BOARD_WAND)
#define HAS_I2C_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 3207
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1783
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 1018 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 566 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (48*1024)
#elif defined(BOARD_MAGNEMITE)
#define HAS_NO_TOUCHPAD
#elif defined(BOARD_MASTERBALL)
#define HAS_I2C_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 2644
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1440
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 839 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 457 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (64*1024)
#elif defined(BOARD_MOONBALL)
#define HAS_I2C_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 2926
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1441
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 950 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 480 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (56*1024)
#elif defined(BOARD_STAFF)
#define HAS_I2C_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 3206
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1832
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 1017 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 581 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (56*1024)
#elif defined(BOARD_WHISKERS)
#define HAS_SPI_TOUCHPAD
#define HAS_EN_PP3300_TP_ACTIVE_HIGH
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 2160
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1573
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 255
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 1030 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 750 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (CONFIG_UPDATE_PDU_SIZE + 128*1024)
/* Enable to send heatmap to AP */
#define CONFIG_USB_ISOCHRONOUS
#elif defined(BOARD_ZED)
/* TODO: update correct parameters */
#define HAS_I2C_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 3207
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1783
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 1018 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 566 /* tenth of mm */
#define CONFIG_TOUCHPAD_VIRTUAL_SIZE (48*1024)
#else
#error "No touchpad information for board."
#endif

/* Assistant key */
#if defined(BOARD_HAMMER) || defined(BOARD_WAND) || defined(BOARD_WHISKERS)
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_ASSISTANT_KEY
#endif

/* Backlight */
#if defined(BOARD_HAMMER) || defined(BOARD_STAFF) || \
    defined(BOARD_WAND) || defined(BOARD_WHISKERS)
/*
 * Even with this option, we detect the backlight presence using a PU/PD on the
 * PWM pin. Not defining this totally disables support.
 */
#define HAS_BACKLIGHT

#ifdef BOARD_WHISKERS
#define CONFIG_LED_DRIVER_LM3630A
#endif

#ifdef BOARD_STAFF
#define KBLIGHT_PWM_FREQ 100 /* Hz */
#else
#define KBLIGHT_PWM_FREQ 50000 /* Hz */
#endif

#endif /* BOARD_HAMMER/WAND/WHISKERS */

/* GMR sensor for tablet mode detection */
#if defined(BOARD_MASTERBALL) || defined(BOARD_MOONBALL) || \
    defined(BOARD_WHISKERS)
#define CONFIG_GMR_TABLET_MODE
#endif

#endif /* SECTION_IS_RW */

#endif /* __CROS_EC_VARIANTS_H */
