/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_TC_SNK_SM_H
#define __CROS_EC_USB_TC_SNK_SM_H

/* Function to retrieve state of servo charger port */
int get_alternate_port_pwr(struct pwr_con_t *pwr);

#endif
