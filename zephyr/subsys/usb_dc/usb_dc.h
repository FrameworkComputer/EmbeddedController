/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB DC Shimming Definitions.
 */

#ifndef __USB_DC_H
#define __USB_DC_H

#include "common.h"

#include <zephyr/usb/usb_ch9.h>

bool check_usb_is_suspended(void);
bool check_usb_is_configured(void);

/**
 * @brief Request usb wake-up
 *
 * @return true if wake up successfully, false otherwise
 */
bool request_usb_wake(void);

#endif
