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
#endif
#ifdef CONFIG_EXTENSION_COMMAND
CONSOLE_CHANNEL(CC_EXTENSION,    "extension")
#endif
CONSOLE_CHANNEL(CC_CHARGER,      "charger")
CONSOLE_CHANNEL(CC_CHIPSET,      "chipset")
CONSOLE_CHANNEL(CC_CLOCK,        "clock")
CONSOLE_CHANNEL(CC_DMA,          "dma")
CONSOLE_CHANNEL(CC_EVENTS,       "events")
#ifdef CONFIG_GESTURE_SW_DETECTION
CONSOLE_CHANNEL(CC_GESTURE,      "gesture")
#endif
CONSOLE_CHANNEL(CC_GPIO,         "gpio")
CONSOLE_CHANNEL(CC_HOSTCMD,      "hostcmd")
CONSOLE_CHANNEL(CC_I2C,          "i2c")
CONSOLE_CHANNEL(CC_KEYBOARD,     "keyboard")
CONSOLE_CHANNEL(CC_KEYSCAN,      "keyscan")
CONSOLE_CHANNEL(CC_LIDANGLE,     "lidangle")
#ifdef HAS_TASK_LIGHTBAR
CONSOLE_CHANNEL(CC_LIGHTBAR,     "lightbar")
#endif
CONSOLE_CHANNEL(CC_LPC,          "lpc")
CONSOLE_CHANNEL(CC_MOTION_LID,   "motionlid")
CONSOLE_CHANNEL(CC_MOTION_SENSE, "motionsense")
#ifdef HAS_TASK_PDCMD
CONSOLE_CHANNEL(CC_PD_HOST_CMD,  "pdhostcm")
#endif
CONSOLE_CHANNEL(CC_PORT80,       "port80")
CONSOLE_CHANNEL(CC_PWM,          "pwm")
CONSOLE_CHANNEL(CC_SPI,          "spi")
#ifdef CONFIG_RBOX
CONSOLE_CHANNEL(CC_RBOX,         "rbox")
#endif
#ifdef CONFIG_SPS
CONSOLE_CHANNEL(CC_SPS,          "sps")
#endif
CONSOLE_CHANNEL(CC_SWITCH,       "switch")
CONSOLE_CHANNEL(CC_SYSTEM,       "system")
CONSOLE_CHANNEL(CC_TASK,         "task")
CONSOLE_CHANNEL(CC_THERMAL,      "thermal")
CONSOLE_CHANNEL(CC_TPM,          "tpm")
CONSOLE_CHANNEL(CC_USB,          "usb")
CONSOLE_CHANNEL(CC_USBCHARGE,    "usbcharge")
CONSOLE_CHANNEL(CC_USBPD,        "usbpd")
CONSOLE_CHANNEL(CC_VBOOT,        "vboot")
CONSOLE_CHANNEL(CC_HOOK,         "hook")
