/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port controller */

#ifndef __CROS_EC_USB_PD_TCPC_H
#define __CROS_EC_USB_PD_TCPC_H

#include "usb_pd_tcpm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* If we are a TCPC but not a TCPM, then we implement the peripheral TCPCI */
#if defined(CONFIG_USB_PD_TCPC) && !defined(CONFIG_USB_PD_TCPM_STUB)
#define TCPCI_I2C_PERIPHERAL
#endif

#ifdef TCPCI_I2C_PERIPHERAL
/* Convert TCPC address to type-C port number */
#define TCPC_ADDR_TO_PORT(addr) \
	((addr)-I2C_STRIP_FLAGS(CONFIG_TCPC_I2C_BASE_ADDR_FLAGS))
/* Check if the i2c address belongs to TCPC */
#define ADDR_IS_TCPC(addr) \
	(((addr) & 0x7E) == I2C_STRIP_FLAGS(CONFIG_TCPC_I2C_BASE_ADDR_FLAGS))
#endif

/**
 * Process incoming TCPCI I2C command
 *
 * @param read This is a read request. If 0, this is a write request.
 * @param len Length of incoming payload
 * @param payload Pointer to incoming and outgoing data
 * @param send_response Function to call to send response if necessary
 */
void tcpc_i2c_process(int read, int port, int len, uint8_t *payload,
		      void (*send_response)(int));

/**
 * Handle VBUS wake interrupts
 *
 * @param signal The VBUS wake interrupt signal
 */
void pd_vbus_evt_p0(enum gpio_signal signal);
void pd_vbus_evt_p1(enum gpio_signal signal);

/*
 * Methods for TCPCI peripherals (e.g. zinger) to get/set their internal
 * state
 */
int tcpc_alert_status(int port, int *alert);
int tcpc_alert_status_clear(int port, uint16_t mask);
int tcpc_alert_mask_set(int port, uint16_t mask);
int tcpc_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
		enum tcpc_cc_voltage_status *cc2);
int tcpc_select_rp_value(int port, int rp);
int tcpc_set_cc(int port, int pull);
int tcpc_set_polarity(int port, int polarity);
int tcpc_set_power_status_mask(int port, uint8_t mask);
int tcpc_set_vconn(int port, int enable);
int tcpc_set_msg_header(int port, int power_role, int data_role);
int tcpc_set_rx_enable(int port, int enable);
int tcpc_get_message(int port, uint32_t *payload, int *head);
int tcpc_transmit(int port, enum tcpci_msg_type type, uint16_t header,
		  const uint32_t *data);
int rx_buf_is_empty(int port);
void rx_buf_clear(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PD_TCPC_H */
