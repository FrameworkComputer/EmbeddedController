/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * AOZ1380 USB-C Power Path Controller
 *
 * This is a basic TCPM controlled PPC driver.  It could easily be
 * renamed and repurposed to be generic, if there are other TCPM
 * controlled PPC chips that are similar to the AOZ1380
 */

#ifndef __CROS_EC_AOZ1380_PUBLIC_H
#define __CROS_EC_AOZ1380_PUBLIC_H

#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ppc_drv;
extern const struct ppc_drv aoz1380_drv;

/**
 * AOZ1380 Set VBus Source Current Limit.
 *
 * Using this driver requires a board_aoz1380_set_vbus_source_limit
 * function due to the lack of programability of this device and
 * requirement for hardware specific code to handle setting this limit.
 *
 * @param port The Type-C port
 * @param rp The Type-C RP value
 * @return EC_SUCCESS for success, otherwise error
 */
int board_aoz1380_set_vbus_source_current_limit(int port,
						enum tcpc_rp_value rp);

/**
 * Interrupt Handler for the AOZ1380.
 *
 * @param port: The Type-C port which triggered the interrupt.
 */
void aoz1380_interrupt(int port);

#ifdef __cplusplus
}
#endif

#endif /* defined(__CROS_EC_AOZ1380_H) */
