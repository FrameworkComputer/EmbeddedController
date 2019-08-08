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
#elif defined(BOARD_STAFF)
#define CONFIG_USB_PID 0x502b
#elif defined(BOARD_WAND)
#define CONFIG_USB_PID 0x502d
#elif defined(BOARD_WHISKERS)
#define CONFIG_USB_PID 0x5030
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
/* TODO(b:138422450): Insert correct dimensions. */
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X 3206
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y 1832
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE 511
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X 1017 /* tenth of mm */
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y 581 /* tenth of mm */
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
#else
#error "No touchpad information for board."
#endif

/* Assistant key */
#if defined(BOARD_HAMMER) || defined(BOARD_WAND) || defined(BOARD_WHISKERS)
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_ASSISTANT_KEY
#endif

/* Backlight driver */
#ifdef BOARD_WHISKERS
#define CONFIG_LED_DRIVER_LM3630A
#endif

/* Hall sensor for tablet mode detection */
#if defined(BOARD_MASTERBALL) || defined(BOARD_WHISKERS)
#define CONFIG_HALL_SENSOR
#endif

#endif /* SECTION_IS_RW */

#endif /* __CROS_EC_VARIANTS_H */
