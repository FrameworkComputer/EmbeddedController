/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_ANX7447_H
#define __CROS_EC_USB_PD_TCPM_ANX7447_H

/* Registers: TCPC slave address used */
#define ANX7447_REG_TCPC_SWITCH_0	0xB4
#define ANX7447_REG_TCPC_SWITCH_1	0xB5
#define ANX7447_REG_TCPC_AUX_SWITCH	0xB6

#define ANX7447_REG_INTR_ALERT_MASK_0	0xC9

#define ANX7447_REG_TCPC_CTRL_2		0xCD
#define ANX7447_REG_ENABLE_VBUS_PROTECT	0x20

#define ANX7447_REG_ADC_CTRL_1		0xBF
#define ANX7447_REG_ADCFSM_EN		0x20

/* Registers: SPI slave address used */
#define ANX7447_REG_HPD_CTRL_0		0x7E
#define ANX7447_REG_HPD_MODE		0x01
#define ANX7447_REG_HPD_OUT		0x02

#define ANX7447_REG_HPD_DEGLITCH_H	0x80
#define ANX7447_REG_HPD_OEN		0x40

#define ANX7447_REG_INTP_CTRL_0		0x9E

struct anx7447_i2c_addr {
	int tcpc_slave_addr;
	int spi_slave_addr;
};

#define AN7447_TCPC0_I2C_ADDR 0x58
#define AN7447_TCPC1_I2C_ADDR 0x56
#define AN7447_TCPC2_I2C_ADDR 0x54
#define AN7447_TCPC3_I2C_ADDR 0x52

#define AN7447_SPI0_I2C_ADDR 0x7E
#define AN7447_SPI1_I2C_ADDR 0x6E
#define AN7447_SPI2_I2C_ADDR 0x64
#define AN7447_SPI3_I2C_ADDR 0x62

int anx7447_set_power_supply_ready(int port);
int anx7447_power_supply_reset(int port);
int anx7447_board_charging_enable(int port, int enable);

void anx7447_hpd_mode_en(int port);
void anx7447_hpd_output_en(int port);

extern const struct tcpm_drv anx7447_tcpm_drv;
extern const struct usb_mux_driver anx7447_usb_mux_driver;
void anx7447_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq);
void anx7447_tcpc_clear_hpd_status(int port);

#endif /* __CROS_EC_USB_PD_TCPM_ANX7688_H */
