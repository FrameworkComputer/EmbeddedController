/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IOEXPANDER_NCT38XX_H
#define __CROS_EC_IOEXPANDER_NCT38XX_H
/*
 * NCT38XX registers are defined in the driver/tcpm/nct38xx.h.
 * No matter they are used by TCPC or IO Expander driver.
 */
#include "nct38xx.h"

/*
 * The interrupt handler to handle Vendor Define ALERT event from IOEX chip.
 *
 * Normally, the Vendor Define event should be checked by the NCT38XX TCPCI
 * driver's tcpc_alert function.
 * This function is only included when NCT38XX TCPC driver is not included.
 * (i.e. CONFIG_USB_PD_TCPM_NCT38XX is not defined)
 */
void nct38xx_ioex_handle_alert(int ioex);

/*
 * Check which IO's interrupt event is triggered. If any, call its
 * registered interrupt handler.
 */
int nct38xx_ioex_event_handler(int ioex);

extern const struct ioexpander_drv nct38xx_ioexpander_drv;

#endif /* defined(__CROS_EC_IOEXPANDER_NCT38XX_H) */
