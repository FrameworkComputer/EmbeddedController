/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USBC_PPC_H
#define __CROS_EC_USBC_PPC_H

#include "common.h"


/* Common APIs for USB Type-C Power Path Controllers (PPC) */

/**
 * Is the port sourcing Vbus?
 *
 * @param port: The type c port.
 * @return 1 if sourcing Vbus, 0 if not.
 */
int ppc_is_sourcing_vbus(int port);

/**
 * Allow current to flow into the system.
 *
 * @param port: The Type-C port's FET to open.
 * @param enable: 1: Turn on the FET, 0: turn off the FET.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_vbus_sink_enable(int port, int enable);

/**
 * Allow current out of the system.
 *
 * @param port: The Type-C port's FET to open.
 * @param enable: 1: Turn on the FET, 0: turn off the FET.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_vbus_source_enable(int port, int enable);

#endif /* !defined(__CROS_EC_USBC_PPC_H) */
