/* Copyright 202 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UEFI_APP_MODE_H
#define __CROS_EC_UEFI_APP_MODE_H

void uefi_app_mode_setting(uint8_t enable);

uint8_t uefi_app_btn_status(void);

#endif	/* __CROS_EC_UEFI_APP_MODE_H */
