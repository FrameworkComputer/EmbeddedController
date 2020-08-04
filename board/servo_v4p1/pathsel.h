/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PATHSEL_H
#define __CROS_EC_PATHSEL_H

/**
 * Both USB3_TypeA0 and USB3_TypeA1 are routed to the DUT by default.
 */
void init_pathsel(void);

/**
 * Routes USB3_TypeA0 port to DUT
 */
void usb3_a0_to_dut(void);

/**
 * Routes USB3_TypeA1 port to DUT
 */
void usb3_a1_to_dut(void);

/**
 * Routes USB3_TypeA0 port to HOST
 */
void usb3_a0_to_host(void);

/**
 * Routes USB3_TypeA1 port to HOST
 */
void usb3_a1_to_host(void);

/**
 * Routes the HOST to the DUT. Used for fastboot
 */
void host_to_dut(void);

/**
 * Routes the Micro Servo to the Host
 */
void uservo_to_host(void);

#endif /* __CROS_EC_PATHSEL_H */
