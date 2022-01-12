/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_PD_TCPM_ANX7406_H
#define __CROS_EC_USB_PD_TCPM_ANX7406_H

#include "usb_mux.h"

struct anx7406_i2c_addr {
	uint16_t tcpc_addr_flags;
	uint16_t top_addr_flags;
};

#define ANX7406_TCPC0_I2C_ADDR_FLAGS (0x58 >> 1)
#define ANX7406_TCPC1_I2C_ADDR_FLAGS (0x56 >> 1)
#define ANX7406_TCPC2_I2C_ADDR_FLAGS (0x54 >> 1)
#define ANX7406_TCPC3_I2C_ADDR_FLAGS (0x52 >> 1)
#define ANX7406_TCPC4_I2C_ADDR_FLAGS (0x90 >> 1)
#define ANX7406_TCPC5_I2C_ADDR_FLAGS (0x9A >> 1)
#define ANX7406_TCPC6_I2C_ADDR_FLAGS (0xA4 >> 1)
#define ANX7406_TCPC7_I2C_ADDR_FLAGS (0xAE >> 1)

#define ANX7406_TOP0_I2C_ADDR_FLAGS (0x7E >> 1)
#define ANX7406_TOP1_I2C_ADDR_FLAGS (0x6E >> 1)
#define ANX7406_TOP2_I2C_ADDR_FLAGS (0x64 >> 1)
#define ANX7406_TOP3_I2C_ADDR_FLAGS (0x62 >> 1)
#define ANX7406_TOP4_I2C_ADDR_FLAGS (0x92 >> 1)
#define ANX7406_TOP5_I2C_ADDR_FLAGS (0x9C >> 1)
#define ANX7406_TOP6_I2C_ADDR_FLAGS (0xA6 >> 1)
#define ANX7406_TOP7_I2C_ADDR_FLAGS (0xB0 >> 1)

/* Registers: TCPC address used */
#define ANX7406_REG_ANALOG_SETTING 0x0C
#define ANX7406_REG_CABLE_DET_DIG BIT(6)
#define ANX7406_REG_DIGITAL_RDY BIT(5)

#define ANX7406_REG_TCPCFILTER 0x9F
#define ANX7406_REG_TCPCCTRL 0xCD
#define ANX7406_REG_TCPCFILTERBIT8 BIT(0)
#define ANX7406_REG_CAP_WP BIT(2)

#define ANX7406_REG_VBUS_SOURCE_CTRL 0xC2
#define SOURCE_GPIO_OEN BIT(2)
#define ANX7406_REG_VBUS_SINK_CTRL 0xC3
#define SINK_GPIO_OEN BIT(2)

#define ANX7406_REG_VBUS_OCP 0xD2
#define OCP_THRESHOLD 0xFF

#define ANX7406_REG_ADC_CTRL_1 0xE3
#define ANX7406_REG_ADC_FSM_EN BIT(0)
#define ANX7406_REG_ADC_MEASURE_VCONN BIT(1)
#define ANX7406_REG_ADC_MEASURE_VBUS BIT(2)
#define ANX7406_REG_ADC_MEASURE_OCP BIT(3)

#define ANX7406_REG_VCONN_CTRL 0xEB
#define VCONN_PWR_CTRL_SEL BIT(2)
#define VCONN_CC2_PWR_ENABLE BIT(1)
#define VCONN_CC1_PWR_ENABLE BIT(0)

/* Registers: TOP address used */
#define ANX7406_REG_HPD_CTRL_0 0x7E
#define ANX7406_REG_HPD_IRQ0 BIT(2)

#define ANX7406_REG_HPD_DEGLITCH_H 0x80
#define ANX7406_REG_HPD_OEN BIT(6)
#define HPD_DEGLITCH_TIME 0x0D

#define EXT_I2C_OP_DELAY 1000
/* Internal I2C0 master */
/* External I2C0 address & offset */
#define EXT_I2C0_ADDR 0x5E
#define EXT_I2C0_OFFSET 0x5F
/* External I2C0 master control bit */
#define EXT_I2C0_CTRL 0x60
#define I2C0_CMD_RESET BIT(6)
#define I2C0_CMD_WRITE BIT(4)
#define I2C0_CMD_READ 0
#define I2C0_CMD_CISCO_READ (BIT(5) | BIT(6))
#define I2C0_SPEED_100K (BIT(2) | BIT(3))
#define I2C0_NO_STOP BIT(1)
#define I2C0_NO_ACK BIT(0)

#define EXT_I2C0_ACCESS_DATA_BYTE_CNT 0x61
#define EXT_I2C0_ACCESS_DATA 0x65

#define EXT_I2C0_ACCESS_CTRL 0x66
#define I2C0_DATA_FULL BIT(7)
#define I2C0_DATA_EMPTY BIT(6)
#define I2C0_TIMING_SET_EN BIT(1)
#define I2C0_DATA_CLR BIT(0)

/* Internal I2C1 master */
/* External I2C1 address & offset */
#define EXT_I2C1_ADDR 0xCC
#define EXT_I2C1_OFFSET 0xCD
/* External I2C1 master control bit */
#define EXT_I2C1_CTRL 0xCE
#define I2C1_CMD_RESET BIT(6)
#define I2C1_CMD_WRITE BIT(4)
#define I2C1_CMD_READ 0
#define I2C1_CMD_CISCO_READ (BIT(5) | BIT(6))
#define I2C1_SPEED_100K (BIT(2) | BIT(3))
#define I2C1_NO_STOP BIT(1)
#define I2C1_NO_ACK BIT(0)

#define EXT_I2C1_ACCESS_DATA_BYTE_CNT 0xCF
#define EXT_I2C1_ACCESS_DATA 0xD3

#define EXT_I2C1_ACCESS_CTRL 0xD4
#define I2C1_DATA_FULL BIT(7)
#define I2C1_DATA_EMPTY BIT(6)
#define I2C1_TIMING_SET_EN BIT(1)
#define I2C1_DATA_CLR BIT(0)

#define I2C1_CISCO_SLAVE 0x80
#define I2C1_CISCO_CTRL_1 0x01
#define VBUS_PROTECT_750MA BIT(1)
#define AUX_PULL_DISABLE BIT(3)

#define I2C1_CISCO_CTRL_3 0x03
#define AUX_FLIP_EN BIT(0)

#define I2C1_CISCO_LOCAL_REG 0x06
#define SELECT_SBU_1_2 BIT(6)

extern const struct tcpm_drv anx7406_tcpm_drv;
int anx7406_set_aux(int port, int flip);
int anx7406_hpd_reset(const int port);
void anx7406_update_hpd_status(const struct usb_mux *mux,
			       mux_state_t mux_state);

#endif /* __CROS_EC_USB_PD_TCPM_ANX7406_H */
