/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __CROS_EC_USB_PD_CONFIG_H
#define __CROS_EC_USB_PD_CONFIG_H

/* Use software CRC */
#define CONFIG_SW_CRC

void pd_select_polarity(int port, int polarity);

void pd_tx_init(void);

void pd_set_host_mode(int port, int enable);

void pd_config_init(int port, uint8_t power_role);

int pd_adc_read(int port, int cc);

#endif /* __CROS_EC_USB_PD_CONFIG_H */
