/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Richtek Type-C port controller */

#ifndef __CROS_EC_DRIVER_TCPM_RT1718S_PUBLIC_H
#define __CROS_EC_DRIVER_TCPM_RT1718S_PUBLIC_H

#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT1718S_I2C_ADDR1_FLAGS 0x43
#define RT1718S_I2C_ADDR2_FLAGS 0x40

#define RT1718S_VID 0x29CF
#define RT1718S_PID 0x1718

#define RT1718S_DEVICE_ID 0x04
#define RT1718S_DEVICE_ID_ES1 0x4511
#define RT1718S_DEVICE_ID_ES2 0x4513

extern const struct tcpm_drv rt1718s_tcpm_drv;
extern const struct bc12_drv rt1718s_bc12_drv;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_TCPM_RT1718S_PUBLIC_H */
