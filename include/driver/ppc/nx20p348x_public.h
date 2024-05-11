/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI NX20P348X USB-C Power Path Controller */

#ifndef __CROS_EC_DRIVER_PPC_NX20P348X_PUBLIC_H
#define __CROS_EC_DRIVER_PPC_NX20P348X_PUBLIC_H

#include "usbc_ppc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NX20P3483_ADDR0_FLAGS 0x70
#define NX20P3483_ADDR1_FLAGS 0x71
#define NX20P3483_ADDR2_FLAGS 0x72
#define NX20P3483_ADDR3_FLAGS 0x73

#define NX20P3481_ADDR0_FLAGS 0x74
#define NX20P3481_ADDR1_FLAGS 0x75
#define NX20P3481_ADDR2_FLAGS 0x76
#define NX20P3481_ADDR3_FLAGS 0x77

extern const struct ppc_drv nx20p348x_drv;

/**
 * Interrupt Handler for the NX20P348x.
 *
 * @param port: The Type-C port which triggered the interrupt.
 */
void nx20p348x_interrupt(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_PPC_NX20P348X_PUBLIC_H */
