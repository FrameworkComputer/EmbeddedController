/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PATHSEL_H
#define __CROS_EC_PATHSEL_H

/**
 * Enables/disables power on A0 USB port
 */
void hh_usb3_a0_pwr_en(int en);

/**
 * Enables/disables power on A1 USB port
 */
void hh_usb3_a1_pwr_en(int en);

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
 * Routes the DUT to the HOST. Used for fastboot
 */
void dut_to_host(void);

/**
 * Routes the Micro Servo to the Host
 */
void uservo_to_host(void);

#endif /* __CROS_EC_PATHSEL_H */
