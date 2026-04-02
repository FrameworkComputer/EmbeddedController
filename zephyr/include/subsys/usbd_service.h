/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef ZEPHYR_INCLUDE_SUBSYS_USBD_SERVICE_H_
#define ZEPHYR_INCLUDE_SUBSYS_USBD_SERVICE_H_

/**
 * Checks whether the USB device controller is suspended.
 *
 * @return true if suspended, false otherwise
 */
bool usb_is_suspended(void);

#endif
