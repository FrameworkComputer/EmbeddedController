/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

/* Port and task configuration */
#define PD_PORT_COUNT 2
#define PORT_TO_TASK_ID(port) ((port) ? TASK_ID_PD_C1 : TASK_ID_PD_C0)
#define TASK_ID_TO_PORT(id)   ((id) == TASK_ID_PD_C0 ? 0 : 1)

/* Use software CRC */
#define CONFIG_SW_CRC

void pd_select_polarity(int port, int polarity);

void pd_tx_init(void);

void pd_set_host_mode(int port, int enable);

void pd_config_init(int port, uint8_t power_role);

int pd_adc_read(int port, int cc);

int pd_snk_is_vbus_provided(int port);

/* Standard-current DFP : no-connect voltage is 1.55V */
#define PD_SRC_VNC 1550 /* mV */

/* UFP-side : threshold for DFP connection detection */
#define PD_SNK_VA   200 /* mV */

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  20000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 20000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

#endif /* __USB_PD_CONFIG_H */
