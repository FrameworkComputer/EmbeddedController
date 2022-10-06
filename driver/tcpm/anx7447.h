/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_mux.h"
#include "driver/tcpm/anx7447_public.h"

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_ANX7447_H
#define __CROS_EC_USB_PD_TCPM_ANX7447_H

/* Registers: TCPC address used */
#define ANX7447_REG_TCPC_SWITCH_0 0xB4
#define ANX7447_REG_TCPC_SWITCH_1 0xB5
#define ANX7447_REG_TCPC_AUX_SWITCH 0xB6
#define VCONN_VOLTAGE_ALARM_HI_CFG 0xB7

#define ANX7447_REG_INTR_ALERT_MASK_0 0xC9

#define ANX7447_REG_TCPC_CTRL_2 0xCD
#define ANX7447_REG_ENABLE_VBUS_PROTECT 0x20

#define ANX7447_REG_ADC_CTRL_1 0xBF
#define ANX7447_REG_ADCFSM_EN 0x20

/* Registers: SPI address used */
#define ANX7447_REG_INTP_SOURCE_0 0x67

#define ANX7447_REG_HPD_CTRL_0 0x7E
#define ANX7447_REG_HPD_MODE 0x01
#define ANX7447_REG_HPD_OUT 0x02
#define ANX7447_REG_HPD_IRQ0 0x04
#define ANX7447_REG_HPD_PLUG 0x08
#define ANX7447_REG_HPD_UNPLUG 0x10

#define ANX7447_REG_HPD_DEGLITCH_H 0x80
#define ANX7447_REG_HPD_DETECT 0x80
#define ANX7447_REG_HPD_OEN 0x40

#define ANX7447_REG_PAD_INTP_CTRL 0x85

#define ANX7447_REG_INTP_MASK_0 0x86

#define ANX7447_REG_ADDR_GPIO_CTRL_1 0x89

#define ANX7447_REG_TCPC_CTRL_1 0x9D
#define CC_DEBOUNCE_MS BIT(3)
#define CC_DEBOUNCE_TIME_HI_BIT BIT(0)
#define ANX7447_REG_INTP_CTRL_0 0x9E
#define ANX7447_REG_CC_DEBOUNCE_TIME 0x9F

#define ANX7447_REG_ANALOG_CTRL_8 0xA8
#define ANX7447_REG_VCONN_OCP_MASK 0x0C
#define ANX7447_REG_VCONN_OCP_240mA 0x00
#define ANX7447_REG_VCONN_OCP_310mA 0x04
#define ANX7447_REG_VCONN_OCP_370mA 0x08
#define ANX7447_REG_VCONN_OCP_440mA 0x0C

#define ANX7447_REG_ANALOG_CTRL_10 0xAA
#define ANX7447_REG_CABLE_DET_DIG 0x40

#define ANX7447_REG_FRSWAP_CTRL 0xAB

#define ANX7447_REG_T_CHK_VBUS_TIMER 0xBB

#define ANX7447_REG_VD_ALERT_MASK 0xC7
#define ANX7447_REG_VD_ALERT 0xC8

#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_MASK 0x38
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_19US 0x00
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_38US 0x08
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_76US 0x10
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_152US 0x18
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_303US 0x20
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_607US 0x28
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_1210US 0x30
#define ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_2430US 0x38

#define ANX7447_REG_ANALOG_CTRL_9 0xA9
#define ANX7447_REG_SAFE_MODE 0x80
#define ANX7447_REG_R_AUX_RES_PULL_SRC 0x20

/* FRSWAP_CTRL bit definitions */
#define ANX7447_FR_SWAP BIT(7)
#define ANX7447_FR_SWAP_EN BIT(6)
#define ANX7447_R_FRSWAP_CONTROL_SELECT BIT(3)
#define ANX7447_R_SIGNAL_FRSWAP BIT(2)
#define ANX7447_TRANSMIT_FRSWAP_SIGNAL BIT(1)
#define ANX7447_FRSWAP_DETECT_ENABLE BIT(0)

/* ADDR_GPIO_CTRL_1 bit definitions */
#define ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA BIT(3)
#define ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_OEN BIT(2)

/* VD_ALERT and VD_ALERT_MASK  bit definitions */
#define ANX7447_TIMER_1_DONE BIT(7)
#define ANX7447_TIMER_0_DONE BIT(6)
#define ANX7447_SOFT_INTP BIT(5)
#define ANX7447_VCONN_VOLTAGE_ALARM_LO BIT(4)
#define ANX7447_VCONN_VOLTAGE_ALARM_HI BIT(3)
#define ANX7447_VCONN_OCP_OCCURRED BIT(2)
#define ANX7447_VBUS_OCP_OCCURRED BIT(1)
#define ANX7447_FRSWAP_SIGNAL_DETECTED BIT(0)

/*
 * This section of defines are only required to support the config option
 * CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND.
 */
/* SPI registers used for OCM flash operations */
#define ANX7447_DELAY_IN_US (20 * 1000)

#define ANX7447_REG_R_RAM_CTRL 0x05
#define ANX7447_REG_R_FLASH_RW_CTRL 0x30
#define ANX7447_REG_R_FLASH_STATUS_0 0x31
#define ANX7447_REG_FLASH_INST_TYPE 0x33
#define ANX7447_REG_FLASH_ERASE_TYPE 0x34
#define ANX7447_REG_OCM_CTRL_0 0x6E
#define ANX7447_REG_ADDR_GPIO_CTRL_0 0x88
#define ANX7447_REG_OCM_MAIN_VERSION 0xB4
#define ANX7447_REG_OCM_BUILD_VERSION 0xB5

/* R_RAM_CTRL bit definitions */
#define ANX7447_R_RAM_CTRL_FLASH_DONE (1 << 7)

/* R_FLASH_RW_CTRL bit definitions */
#define ANX7447_R_FLASH_RW_CTRL_GENERAL_INST_EN (1 << 6)
#define ANX7447_R_FLASH_RW_CTRL_FLASH_ERASE_EN (1 << 5)
#define ANX7447_R_FLASH_RW_CTRL_WRITE_STATUS_EN (1 << 2)
#define ANX7447_R_FLASH_RW_CTRL_FLASH_READ (1 << 1)
#define ANX7447_R_FLASH_RW_CTRL_FLASH_WRITE (1 << 0)

/* R_FLASH_STATUS_0 definitions */
#define ANX7447_FLASH_STATUS_SPI_STATUS_0 0x43

/* FLASH_ERASE_TYPE bit definitions */
#define ANX7447_FLASH_INST_TYPE_WRITEENABLE 0x06
#define ANX7447_FLASH_ERASE_TYPE_CHIPERASE 0x60

/* OCM_CTRL_0 bit definitions */
#define ANX7447_OCM_CTRL_OCM_RESET (1 << 6)

/* ADDR_GPIO_CTRL_0 bit definitions */
#define ANX7447_ADDR_GPIO_CTRL_0_SPI_WP (1 << 7)
#define ANX7447_ADDR_GPIO_CTRL_0_SPI_CLK_ENABLE (1 << 6)
/* End of defines used for CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND */

struct anx7447_i2c_addr {
	uint16_t tcpc_addr_flags;
	uint16_t spi_addr_flags;
};

/*
 * Time TEST_R must be held high for a reset
 */
#define ANX74XX_RESET_HOLD_MS 1
/*
 * Time after TEST_R reset to wait for eFuse loading
 */
#define ANX74XX_RESET_FINISH_MS 2

int anx7447_set_power_supply_ready(int port);
int anx7447_power_supply_reset(int port);
int anx7447_board_charging_enable(int port, int enable);

void anx7447_hpd_mode_en(int port);
void anx7447_hpd_output_en(int port);

void anx7447_tcpc_clear_hpd_status(int port);
void anx7447_tcpc_update_hpd_status(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required);

/**
 * Erase OCM flash if it's not empty
 *
 * @param port: USB-C port number
 * @return: EC_SUCCESS or EC_ERROR_*
 */
int anx7447_flash_erase(int port);

#endif /* __CROS_EC_USB_PD_TCPM_ANX7688_H */
