/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_RESET_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_RESET_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset the sensor IC.
 *
 */
void egis_fp_reset_sensor(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_RESET_H_ */
