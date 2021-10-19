/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Silergy SYV682x Type-C Power Path Controller */

#ifndef __CROS_EC_DRIVER_PPC_SYV682X_PUBLIC_H
#define __CROS_EC_DRIVER_PPC_SYV682X_PUBLIC_H

/* I2C addresses */
#define SYV682X_ADDR0_FLAGS		0x40
#define SYV682X_ADDR1_FLAGS		0x41
#define SYV682X_ADDR2_FLAGS		0x42
#define SYV682X_ADDR3_FLAGS		0x43

extern const struct ppc_drv syv682x_drv;

void syv682x_interrupt(int port);

#endif /* __CROS_EC_DRIVER_PPC_SYV682X_PUBLIC_H */
