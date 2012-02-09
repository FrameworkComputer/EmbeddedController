/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#ifndef __CROS_EC_PORT80_H
#define __CROS_EC_PORT80_H

#include "common.h"

/* Initializes the module. */
int port_80_init(void);

/* Called by LPC module when a byte of data is written to port 80. */
void port_80_write(int data);

#endif  /* __CROS_EC_PORT80_H */
