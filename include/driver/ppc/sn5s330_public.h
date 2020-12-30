/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI SN5S330 USB-C Power Path Controller */

#ifndef __CROS_EC_DRIVER_PPC_SN5S330_PUBLIC_H
#define __CROS_EC_DRIVER_PPC_SN5S330_PUBLIC_H

#define SN5S330_ADDR0_FLAGS 0x40
#define SN5S330_ADDR1_FLAGS 0x41
#define SN5S330_ADDR2_FLAGS 0x42
#define SN5S330_ADDR3_FLAGS 0x43

extern const struct ppc_drv sn5s330_drv;

/**
 * Interrupt Handler for the SN5S330.
 *
 * By default, the only interrupt sources that are unmasked are overcurrent
 * conditions for PP1, and VBUS_GOOD if PPC is being used to detect VBUS
 * (CONFIG_USB_PD_VBUS_DETECT_PPC).
 *
 * @param port: The Type-C port which triggered the interrupt.
 */
void sn5s330_interrupt(int port);

#endif /* __CROS_EC_DRIVER_PPC_SN5S330_PUBLIC_H */
