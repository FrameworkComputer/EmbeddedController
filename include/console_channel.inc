/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

CONSOLE_CHANNEL(CC_COMMAND,      "command")
CONSOLE_CHANNEL(CC_ACCEL,        "accel")
#ifdef CONFIG_BLUETOOTH_LE
CONSOLE_CHANNEL(CC_BLUETOOTH_LE, "bluetooth_le")
#ifdef CONFIG_BLUETOOTH_LL_DEBUG
CONSOLE_CHANNEL(CC_BLUETOOTH_LL, "bluetooth_ll")
#endif
#ifdef CONFIG_BLUETOOTH_HCI_DEBUG
CONSOLE_CHANNEL(CC_BLUETOOTH_HCI,"bluetooth_hci")
#endif
#endif /* CONFIG_BLUETOOTH_LE */
#ifdef CONFIG_CEC
CONSOLE_CHANNEL(CC_CEC, "cec")
#endif
#ifdef CONFIG_EXTENSION_COMMAND
CONSOLE_CHANNEL(CC_EXTENSION,    "extension")
#endif
#if defined(CONFIG_PHYSICAL_PRESENCE)
CONSOLE_CHANNEL(CC_CCD,          "ccd")
#endif
CONSOLE_CHANNEL(CC_CHARGER,      "charger")
CONSOLE_CHANNEL(CC_CHIPSET,      "chipset")
CONSOLE_CHANNEL(CC_CLOCK,        "clock")
#ifdef CONFIG_DMA
CONSOLE_CHANNEL(CC_DMA,          "dma")
#endif
CONSOLE_CHANNEL(CC_EVENTS,       "events")
#ifdef HAS_TASK_FPSENSOR
CONSOLE_CHANNEL(CC_FP,           "fingerprint")
#endif
#ifdef CONFIG_GESTURE_SW_DETECTION
CONSOLE_CHANNEL(CC_GESTURE,      "gesture")
#endif
CONSOLE_CHANNEL(CC_GPIO,         "gpio")
#ifdef HAS_TASK_HOSTCMD
CONSOLE_CHANNEL(CC_HOSTCMD,      "hostcmd")
#endif
#ifdef CONFIG_I2C
CONSOLE_CHANNEL(CC_I2C,          "i2c")
#endif
CONSOLE_CHANNEL(CC_KEYBOARD,     "keyboard")
CONSOLE_CHANNEL(CC_KEYSCAN,      "keyscan")
CONSOLE_CHANNEL(CC_LIDANGLE,     "lidangle")
#ifdef HAS_TASK_LIGHTBAR
CONSOLE_CHANNEL(CC_LIGHTBAR,     "lightbar")
#endif
#ifdef CONFIG_HOSTCMD_X86
CONSOLE_CHANNEL(CC_LPC,          "lpc")
#endif
CONSOLE_CHANNEL(CC_MOTION_LID,   "motionlid")
CONSOLE_CHANNEL(CC_MOTION_SENSE, "motionsense")
#ifdef HAS_TASK_PDCMD
CONSOLE_CHANNEL(CC_PD_HOST_CMD,  "pdhostcm")
#endif
CONSOLE_CHANNEL(CC_PORT80,       "port80")
#if defined(CONFIG_PWM) || defined(CONFIG_FANS)
CONSOLE_CHANNEL(CC_PWM,          "pwm")
#endif
CONSOLE_CHANNEL(CC_SPI,          "spi")
#ifdef CONFIG_RBOX
CONSOLE_CHANNEL(CC_RBOX,         "rbox")
#endif
#ifdef CONFIG_SPS
CONSOLE_CHANNEL(CC_SPS,          "sps")
#endif
#if defined(CONFIG_SWITCH) || defined(CONFIG_LID_SWITCH)
CONSOLE_CHANNEL(CC_SWITCH,       "switch")
#endif
CONSOLE_CHANNEL(CC_SYSTEM,       "system")
CONSOLE_CHANNEL(CC_TASK,         "task")
#ifdef CONFIG_TOUCHPAD
CONSOLE_CHANNEL(CC_TOUCHPAD,     "touchpad")
#endif
#ifdef CONFIG_DPTF
CONSOLE_CHANNEL(CC_DPTF,         "dptf")
#endif
#ifdef CONFIG_ALS
CONSOLE_CHANNEL(CC_ALS,		 "als")
#endif
CONSOLE_CHANNEL(CC_THERMAL,      "thermal")
CONSOLE_CHANNEL(CC_TPM,          "tpm")
CONSOLE_CHANNEL(CC_USB,          "usb")
CONSOLE_CHANNEL(CC_USBCHARGE,    "usbcharge")
#if defined(CONFIG_USB_POWER_DELIVERY) || defined(CONFIG_USB_PD_TCPC)
CONSOLE_CHANNEL(CC_USBPD,        "usbpd")
#endif
CONSOLE_CHANNEL(CC_VBOOT,        "vboot")
#ifdef CONFIG_WAKE_ON_VOICE
CONSOLE_CHANNEL(CC_WOV,          "wov")
#endif
CONSOLE_CHANNEL(CC_HOOK,         "hook")
