/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_ANX7688_H
#define __CROS_EC_USB_PD_TCPM_ANX7688_H

int anx7688_update_hpd(int port, int level, int irq);
int anx7688_set_dp_pin_mode(int port, int pin_mode);
int anx7688_enable_cable_detection(int port);
int anx7688_set_power_supply_ready(int port);
int anx7688_power_supply_reset(int port);
int anx7688_hpd_disable(int port);

extern struct tcpm_drv anx7688_tcpm_drv;
extern struct usb_mux_driver anx7688_usb_mux_driver;

#endif /* __CROS_EC_USB_PD_TCPM_ANX7688_H */
