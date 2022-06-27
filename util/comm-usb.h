/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Header file for a USB interface.
 */

#ifndef __UTIL_COMM_USB_H
#define __UTIL_COMM_USB_H

#include "common.h"

/**
 * Parse USB vendor ID and product ID pair (e.g. '18d1:5022').
 *
 * @param input    Input string to be parsed.
 * @param vid_ptr  Parsed vendor ID.
 * @param pid_ptr  Parsed product ID
 * @return 1 if parsed successfully or 0 on error.
 */
int parse_vidpid(const char *input, uint16_t *vid_ptr, uint16_t *pid_ptr);

/**
 * Initialize USB communication.
 *
 * @param vid  Vendor ID of the endpoint device.
 * @param pid  Product ID of the endpoint device.
 * @return     Zero if success or non-zero otherwise.
 */
int comm_init_usb(uint16_t vid, uint16_t pid);

/**
 * Clean up USB communication.
 */
void comm_usb_exit(void);

#endif /* __UTIL_COMM_USB_H */
