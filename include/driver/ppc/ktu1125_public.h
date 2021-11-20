/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kinetic KTU1125 USB-C Power Path Controller */

#ifndef __CROS_EC_DRIVER_PPC_KTU1125_PUBLIC_H
#define __CROS_EC_DRIVER_PPC_KTU1125_PUBLIC_H

#define KTU1125_ADDR0_FLAGS 0x78
#define KTU1125_ADDR1_FLAGS 0x79
#define KTU1125_ADDR2_FLAGS 0x7A
#define KTU1125_ADDR3_FLAGS 0x7B

extern const struct ppc_drv ktu1125_drv;

/**
 * Interrupt Handler for the KTU1125.
 *
 * @param port: The Type-C port which triggered the interrupt.
 */
void ktu1125_interrupt(int port);

#endif /* __CROS_EC_DRIVER_PPC_KTU1125_PUBLIC_H */
