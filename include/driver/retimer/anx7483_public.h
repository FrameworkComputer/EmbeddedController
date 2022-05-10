/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7483: Active redriver with linear equilzation
 *
 * Public functions, definitions, and structures.
 */

#ifndef __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H
#define __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H

#include "usb_mux.h"

/* I2C interface addresses */
#define ANX7483_I2C_ADDR0_FLAGS		0x3E
#define ANX7483_I2C_ADDR1_FLAGS		0x38
#define ANX7483_I2C_ADDR2_FLAGS		0x40
#define ANX7483_I2C_ADDR3_FLAGS		0x44

/* Configure datasheet defaults for tuning registers at this mux setting */
enum ec_error_list anx7483_set_default_tuning(const struct usb_mux *me,
					      mux_state_t mux_state);

extern const struct usb_mux_driver anx7483_usb_retimer_driver;
#endif /* __CROS_EC_USB_RETIMER_ANX7483_PUBLIC_H */
