/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Common functions for UFP-D devices.
 */

#ifndef __CROS_EC_USB_PD_DP_UFP_H
#define __CROS_EC_USB_PD_DP_UFP_H

struct hpd_to_pd_config_t {
	int port;
	enum gpio_signal signal;
};

extern const struct hpd_to_pd_config_t hpd_config;
/*
 * Function used to handle hpd gpio interrupts.
 *
 * @param signal -> gpio signal associated with hpd interrupt
 */
void usb_pd_hpd_edge_event(int signal);

/*
 * Function used to enable/disable the hpd->dp attention protocol converter
 *    - called with enable when enter mode command is processed.
 *    - called with disable when exit mode command is processed.
 *
 * @param enable -> converter on/off
 */
void usb_pd_hpd_converter_enable(int enable);

#endif  /* __CROS_EC_USB_PD_DP_UFP_H */
