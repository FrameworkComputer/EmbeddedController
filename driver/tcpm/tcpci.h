/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_TCPCI_H
#define __CROS_EC_USB_PD_TCPM_TCPCI_H

#include "tcpm.h"
#include "usb_mux.h"

#define TCPC_REG_VENDOR_ID         0x0
#define TCPC_REG_PRODUCT_ID        0x2
#define TCPC_REG_BCD_DEV           0x4
#define TCPC_REG_TC_REV            0x6
#define TCPC_REG_PD_REV            0x8
#define TCPC_REG_PD_INT_REV        0xa
#define TCPC_REG_ALERT             0x10

#define TCPC_REG_ALERT_MASK_ALL     0xfff
#define TCPC_REG_ALERT_VBUS_DISCNCT (1<<11)
#define TCPC_REG_ALERT_RX_BUF_OVF   (1<<10)
#define TCPC_REG_ALERT_FAULT        (1<<9)
#define TCPC_REG_ALERT_V_ALARM_LO   (1<<8)
#define TCPC_REG_ALERT_V_ALARM_HI   (1<<7)
#define TCPC_REG_ALERT_TX_SUCCESS   (1<<6)
#define TCPC_REG_ALERT_TX_DISCARDED (1<<5)
#define TCPC_REG_ALERT_TX_FAILED    (1<<4)
#define TCPC_REG_ALERT_RX_HARD_RST  (1<<3)
#define TCPC_REG_ALERT_RX_STATUS    (1<<2)
#define TCPC_REG_ALERT_POWER_STATUS (1<<1)
#define TCPC_REG_ALERT_CC_STATUS    (1<<0)
#define TCPC_REG_ALERT_TX_COMPLETE  (TCPC_REG_ALERT_TX_SUCCESS | \
				      TCPC_REG_ALERT_TX_DISCARDED | \
				      TCPC_REG_ALERT_TX_FAILED)

#define TCPC_REG_ALERT_MASK        0x12
#define TCPC_REG_POWER_STATUS_MASK 0x14
#define TCPC_REG_FAULT_STATUS_MASK 0x15
#define TCPC_REG_CONFIG_STD_OUTPUT 0x18

#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK (3 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_NONE (0 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB  (1 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP   (2 << 2)

#define TCPC_REG_TCPC_CTRL         0x19
#define TCPC_REG_TCPC_CTRL_SET(polarity) (polarity)
#define TCPC_REG_TCPC_CTRL_POLARITY(reg) ((reg) & 0x1)

#define TCPC_REG_ROLE_CTRL         0x1a
#define TCPC_REG_ROLE_CTRL_SET(drp, rp, cc1, cc2) \
		((drp) << 6 | (rp) << 4 | (cc2) << 2 | (cc1))
#define TCPC_REG_ROLE_CTRL_CC2(reg) (((reg) & 0xc) >> 2)
#define TCPC_REG_ROLE_CTRL_CC1(reg) ((reg) & 0x3)

#define TCPC_REG_FAULT_CTRL        0x1b
#define TCPC_REG_POWER_CTRL        0x1c
#define TCPC_REG_POWER_CTRL_SET(vconn) (vconn)
#define TCPC_REG_POWER_CTRL_VCONN(reg)    ((reg) & 0x1)

#define TCPC_REG_CC_STATUS         0x1d
#define TCPC_REG_CC_STATUS_SET(term, cc1, cc2) \
		((term) << 4 | ((cc2) & 0x3) << 2 | ((cc1) & 0x3))
#define TCPC_REG_CC_STATUS_TERM(reg) (((reg) & 0x10) >> 4)
#define TCPC_REG_CC_STATUS_CC2(reg)  (((reg) & 0xc) >> 2)
#define TCPC_REG_CC_STATUS_CC1(reg)  ((reg) & 0x3)

#define TCPC_REG_POWER_STATUS      0x1e
#define TCPC_REG_POWER_STATUS_MASK_ALL  0xff
#define TCPC_REG_POWER_STATUS_VBUS_PRES (1<<2)
#define TCPC_REG_POWER_STATUS_VBUS_DET  (1<<3)
#define TCPC_REG_POWER_STATUS_UNINIT    (1<<6)
#define TCPC_REG_FAULT_STATUS      0x1f

#define TCPC_REG_COMMAND           0x23
#define TCPC_REG_DEV_CAP_1         0x24
#define TCPC_REG_DEV_CAP_2         0x26
#define TCPC_REG_STD_INPUT_CAP     0x28
#define TCPC_REG_STD_OUTPUT_CAP    0x29

#define TCPC_REG_MSG_HDR_INFO      0x2e
#define TCPC_REG_MSG_HDR_INFO_SET(drole, prole) \
		((drole) << 3 | (PD_REV20 << 1) | (prole))
#define TCPC_REG_MSG_HDR_INFO_DROLE(reg) (((reg) & 0x8) >> 3)
#define TCPC_REG_MSG_HDR_INFO_PROLE(reg) ((reg) & 0x1)

#define TCPC_REG_RX_DETECT         0x2f
#define TCPC_REG_RX_DETECT_SOP_HRST_MASK 0x21
#define TCPC_REG_RX_BYTE_CNT       0x30
#define TCPC_REG_RX_BUF_FRAME_TYPE 0x31

#define TCPC_REG_RX_HDR            0x32
#define TCPC_REG_RX_DATA           0x34 /* through 0x4f */

#define TCPC_REG_TRANSMIT          0x50
#define TCPC_REG_TRANSMIT_SET(type) \
		(PD_RETRY_COUNT << 4 | (type))
#define TCPC_REG_TRANSMIT_RETRY(reg) (((reg) & 0x30) >> 4)
#define TCPC_REG_TRANSMIT_TYPE(reg)  ((reg) & 0x7)

#define TCPC_REG_TX_BYTE_CNT       0x51
#define TCPC_REG_TX_HDR            0x52
#define TCPC_REG_TX_DATA           0x54 /* through 0x6f */

#define TCPC_REG_VBUS_VOLTAGE                0x70
#define TCPC_REG_VBUS_SINK_DISCONNECT_THRESH 0x72
#define TCPC_REG_VBUS_STOP_DISCHARGE_THRESH  0x74
#define TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG   0x76
#define TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG   0x78

extern const struct tcpm_drv tcpci_tcpm_drv;
extern const struct usb_mux_driver tcpci_tcpm_usb_mux_driver;

int tcpci_tcpm_get_cc(int port, int *cc1, int *cc2);
int tcpci_tcpm_get_vbus_level(int port);
int tcpci_tcpm_set_cc(int port, int pull);
int tcpci_tcpm_set_polarity(int port, int polarity);
int tcpci_tcpm_set_vconn(int port, int enable);
int tcpci_tcpm_set_msg_header(int port, int power_role, int data_role);
int tcpci_tcpm_set_rx_enable(int port, int enable);
int tcpci_tcpm_get_message(int port, uint32_t *payload, int *head);
int tcpci_tcpm_transmit(int port, enum tcpm_transmit_type type,
			uint16_t header, const uint32_t *data);

int tcpci_tcpm_mux_init(int i2c_addr);
int tcpci_tcpm_mux_set(int i2c_addr, mux_state_t mux_state);
int tcpci_tcpm_mux_get(int i2c_addr, mux_state_t *mux_state);

#endif /* __CROS_EC_USB_PD_TCPM_TCPCI_H */
