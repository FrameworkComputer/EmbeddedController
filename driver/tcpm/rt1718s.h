/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_PD_TCPM_RT1718S_H
#define __CROS_EC_USB_PD_TCPM_RT1718S_H

#include "tcpm/rt1718s_public.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "util.h"

/* RT1718S Private RegMap */
#define RT1718S_PHYCTRL1 0x80
#define RT1718S_PHYCTRL2 0x81
#define RT1718S_PHYCTRL3 0x82
#define RT1718S_PHYCTRL7 0x86
#define RT1718S_VCON_CTRL1 0x8A
#define RT1718S_VCON_CTRL3 0x8C
#define RT1718S_VCON_LIMIT_MODE BIT(0)
#define RT1718S_SYS_CTRL1 0x8F
#define RT1718S_SYS_CTRL1_TCPC_CONN_INVALID BIT(6)
#define RT1718S_SYS_CTRL1_SHIPPING_OFF BIT(5)
#define RT1718S_SYS_CTRL2 0x90
#define RT1718S_SYS_CTRL2_BMCIO_OSC_EN BIT(0)
#define RT1718S_SYS_CTRL2_LPWR_EN BIT(3)

#define RT1718S_VCONN_CONTROL_2 0x8B
#define RT1718S_VCONN_CONTROL_2_OVP_EN_CC1 BIT(7)
#define RT1718S_VCONN_CONTROL_2_OVP_EN_CC2 BIT(6)
#define RT1718S_VCONN_CONTROL_2_RVP_EN BIT(3)
#define RT1718S_VCONN_CONTROL_3 0x8C
#define RT1718S_VCONN_CONTROL_3_VCONN_OCP_SEL GENMASK(7, 5)
#define RT1718S_VCONN_CONTROL_3_VCONN_OVP_DEG BIT(1)

#define RT1718S_SYS_CTRL2 0x90
#define RT1718S_SYS_CTRL2_VCONN_DISCHARGE_EN BIT(5)

#define RT1718S_RT_MASK1 0x91
#define RT1718S_RT_MASK1_M_VBUS_FRS_LOW BIT(7)
#define RT1718S_RT_MASK1_M_RX_FRS BIT(6)
#define RT1718S_RT_MASK2 0x92
#define RT1718S_RT_MASK3 0x93
#define RT1718S_RT_MASK4 0x94
#define RT1718S_RT_MASK5 0x95
#define RT1718S_RT_MASK6 0x96
#define RT1718S_RT_MASK6_M_BC12_SNK_DONE BIT(7)
#define RT1718S_RT_MASK6_M_HVDCP_CHK_DONE BIT(6)
#define RT1718S_RT_MASK6_M_BC12_TA_CHG BIT(5)
#define RT1718S_RT_MASK7 0x97

#define RT1718S_RT_INT1 0x98
#define RT1718S_RT_INT1_INT_VBUS_FRS_LOW BIT(7)
#define RT1718S_RT_INT1_INT_RX_FRS BIT(6)
#define RT1718S_RT_INT2 0x99
#define RT1718S_RT_INT6 0x9D
#define RT1718S_RT_INT6_INT_BC12_SNK_DONE BIT(7)
#define RT1718S_RT_INT6_INT_HVDCP_CHK_DONE BIT(6)
#define RT1718S_RT_INT6_INT_BC12_TA_CHG BIT(5)
#define RT1718S_RT_INT6_INT_ADC_DONE BIT(0)

#define RT1718S_RT_ST6 0xA4
#define RT1718S_RT_ST6_BC12_SNK_DONE BIT(7)
#define RT1718S_RT_ST6_HVDCP_CHK_DONE BIT(6)
#define RT1718S_RT_ST6_BC12_TA_CHG BIT(5)

#define RT1718S_PHYCTRL9 0xAC

#define RT1718S_SYS_CTRL3 0xB0
#define RT1718S_TCPC_CTRL1 0xB1
#define RT1718S_TCPC_CTRL2 0xB2
#define RT1718S_TCPC_CTRL3 0xB3
#define RT1718S_SWRESET_MASK BIT(0)
#define RT1718S_TCPC_CTRL4 0xB4
#define RT1718S_SYS_CTRL4 0xB8
#define RT1718S_WATCHDOG_CTRL 0xBE
#define RT1718S_I2C_RST_CTRL 0xBF

#define RT1718S_HILO_CTRL9 0xC8
#define RT1718S_SHILED_CTRL1 0xCA
#define RT1718S_FRS_CTRL1 0xCB
#define RT1718S_FRS_CTRL1_FRSWAPRX_MASK 0xF0
#define RT1718S_FRS_CTRL2 0xCC
#define RT1718S_FRS_CTRL2_RX_FRS_EN BIT(6)
#define RT1718S_FRS_CTRL2_FR_VBUS_SELECT BIT(4)
#define RT1718S_FRS_CTRL2_VBUS_FRS_EN BIT(3)
#define RT1718S_FRS_CTRL3 0xCE
#define RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 BIT(3)
#define RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1 BIT(2)

#define RT1718S_DIS_SRC_VBUS_CTRL 0xE0
#define RT1718S_ENA_SRC_VBUS_CTRL 0xE1
#define RT1718S_FAULT_OC1_VBUS_CTRL 0xE3
#define RT1718S_GPIO1_VBUS_CTRL 0xEA
#define RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS BIT(6)
#define RT1718S_GPIO_VBUS_CTRL_FRS_TX_VBUS BIT(5)
#define RT1718S_GPIO_VBUS_CTRL_ENA_SRC_HV_VBUS_GPIO BIT(4)
#define RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO BIT(3)
#define RT1718S_GPIO_VBUS_CTRL_DIS_SRC_VBUS_GPIO BIT(2)
#define RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO BIT(1)
#define RT1718S_GPIO_VBUS_CTRL_DIS_SNK_VBUS_GPIO BIT(0)
#define RT1718S_GPIO2_VBUS_CTRL 0xEB
#define RT1718S_VBUS_CTRL_EN 0xEC
#define RT1718S_VBUS_CTRL_EN_GPIO2_VBUS_PATH_EN BIT(7)
#define RT1718S_VBUS_CTRL_EN_GPIO1_VBUS_PATH_EN BIT(6)

#define RT1718S_GPIO_CTRL(n) (0xED + (n))
#define RT1718S_GPIO_CTRL_PU BIT(5)
#define RT1718S_GPIO_CTRL_PD BIT(4)
#define RT1718S_GPIO_CTRL_OD_N BIT(3)
#define RT1718S_GPIO_CTRL_OE BIT(2)
#define RT1718S_GPIO_CTRL_O BIT(1)
#define RT1718S_GPIO_CTRL_I BIT(0)

#define RT1718S_UNLOCK_PW_2 0xF0
#define RT1718S_UNLOCK_PW_1 0xF1

#define RT1718S_RT2 0xF2

#define RT1718S_RT2_SYS_CTRL5 0xF210

#define RT1718S_VBUS_VOL_TO_REG(_vol) (CLAMP(_vol, 5, 20) - 5)
#define RT1718S_VBUS_PCT_TO_REG(_pct) (CLAMP(_pct, 5, 20) / 5 - 1)
#define RT1718S_RT2_VBUS_VOL_CTRL 0xF213
#define RT1718S_RT2_VBUS_VOL_CTRL_OVP_SEL (BIT(5) | BIT(4))
#define RT1718S_RT2_VBUS_VOL_CTRL_VOL_SEL 0x0F

#define RT1718S_VCON_CTRL4 0xF211
#define RT1718S_VCON_CTRL4_UVP_CP_EN BIT(5)
#define RT1718S_VCON_CTRL4_OCP_CP_EN BIT(4)

#define RT1718S_RT2_VBUS_OCRC_EN 0xF214
#define RT1718S_RT2_VBUS_OCRC_EN_VBUS_OCP1_EN BIT(0)
#define RT1718S_RT2_VBUS_OCP_CTRL1 0xF216
#define RT1718S_RT2_VBUS_OCP_CTRL4 0xF219

#define RT1718S_RT2_SBU_CTRL_01 0xF23A
#define RT1718S_RT2_SBU_CTRL_01_SBU_VIEN BIT(7)
#define RT1718S_RT2_SBU_CTRL_01_DPDM_VIEN BIT(6)
#define RT1718S_RT2_SBU_CTRL_01_SBU2_SWEN BIT(3)
#define RT1718S_RT2_SBU_CTRL_01_SBU1_SWEN BIT(2)
#define RT1718S_RT2_SBU_CTRL_01_DM_SWEN BIT(1)
#define RT1718S_RT2_SBU_CTRL_01_DP_SWEN BIT(0)

#define RT1718S_RT2_BC12_SNK_FUNC 0xF260
#define RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN BIT(7)
#define RT1718S_RT2_BC12_SNK_FUNC_SPEC_TA_EN BIT(6)
#define RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_MASK 0x30
#define RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_DISABLE 0x00
#define RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_300MS 0x10
#define RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_600MS 0x20
#define RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_WAIT_DATA 0x30
#define RT1718S_RT2_BC12_SNK_FUNC_VLGC_OPT BIT(3)
#define RT1718S_RT2_BC12_SNK_FUNC_VPORT_SEL BIT(2)
#define RT1718S_RT2_BC12_SNK_FUNC_BC12_WAIT_VBUS BIT(1)

#define RT1718S_RT2_BC12_STAT 0xF261
#define RT1718S_RT2_BC12_STAT_DCDT BIT(4)
#define RT1718S_RT2_BC12_STAT_PORT_STATUS_MASK 0x0F
#define RT1718S_RT2_BC12_STAT_PORT_STATUS_NONE 0x00
#define RT1718S_RT2_BC12_STAT_PORT_STATUS_SDP 0x0D
#define RT1718S_RT2_BC12_STAT_PORT_STATUS_CDP 0x0E
#define RT1718S_RT2_BC12_STAT_PORT_STATUS_DCP 0x0F

#define RT1718S_RT2_DPDM_CTR1_DPDM_SET 0xF263
#define RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_MASK 0x03
#define RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_55V 0x00
#define RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_60V 0x01
#define RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_65V 0x02
#define RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_70V 0x03

#define RT1718S_RT2_BC12_SRC_FUNC 0xF26D
#define RT1718S_RT2_BC12_SRC_FUNC_BC12_SRC_EN BIT(7)
#define RT1718S_RT2_BC12_SRC_FUNC_SRC_MODE_SEL_MASK 0x70
#define RT1718S_RT2_BC12_SRC_FUNC_SRC_MODE_SEL_BC12_SDP 0x00
#define RT1718S_RT2_BC12_SRC_FUNC_SRC_MODE_SEL_BC12_CDP 0x10
#define RT1718S_RT2_BC12_SRC_FUNC_SRC_MODE_SEL_BC12_DCP 0x20
#define RT1718S_RT2_BC12_SRC_FUNC_WAIT_VBUS_ON BIT(0)

#define RT1718S_ADC_CTRL_01 0xF2A0
#define RT1718S_ADC_CTRL_02 0xF2A1
#define RT1718S_ADC_CHX_VOL_L(ch) (0xF2A6 + (ch) * 2)
#define RT1718S_ADC_CHX_VOL_H(ch) (0xF2A7 + (ch) * 2)

int rt1718s_write8(int port, int reg, int val);
int rt1718s_read8(int port, int reg, int *val);
int rt1718s_update_bits8(int port, int reg, int mask, int val);
int rt1718s_write16(int port, int reg, int val);
int rt1718s_read16(int port, int reg, int *val);
__override_proto int board_rt1718s_init(int port);

enum rt1718s_adc_channel {
	RT1718S_ADC_VBUS1 = 0,
	RT1718S_ADC_VBUS2,
	RT1718S_ADC_VDC,
	RT1718S_ADC_VBUS_CURRENT,
	RT1718S_ADC_CC1,
	RT1718S_ADC_CC2,
	RT1718S_ADC_SBU1,
	RT1718S_ADC_SBU2,
	RT1718S_ADC_DP,
	RT1718S_ADC_DM,
	RT1718S_ADC_CH10,
	RT1718S_ADC_CH11,
};

int rt1718s_get_adc(int port, enum rt1718s_adc_channel channel, int *adc_val);

enum rt1718s_gpio {
	RT1718S_GPIO1 = 0,
	RT1718S_GPIO2,
	RT1718S_GPIO3,
	RT1718S_GPIO_COUNT,
};

/**
 * Set flags for GPIO
 *
 * @param port		rt1718s I2C port
 * @param signal	gpio pin name in enum rt1718s_gpio
 * @param flags		GPIO_* flags defined in include/gpio.h
 */
void rt1718s_gpio_set_flags(int port, enum rt1718s_gpio signal, uint32_t flags);

/**
 * Set the value of a signal
 *
 * @param port		rt1718s I2C port
 * @param signal	gpio pin name in enum rt1718s_gpio
 * @param value		New value for signal (0 = low, non-zero = high)
 */
void rt1718s_gpio_set_level(int port, enum rt1718s_gpio signal, int value);

/**
 * Get the current value of a signal.
 *
 * @param port		rt1718s I2C port
 * @param signal	gpio pin name in enum rt1718s_gpio
 * @return 0 if low, 1 if high.
 */
int rt1718s_gpio_get_level(int port, enum rt1718s_gpio signal);

/**
 * Set fast role swap.
 *
 * @param port		USB-C port
 * @param enable	enable/disable FRS
 * @return EC_SUCCESS if success, EC_ERROR_UNKNOWN otherwise.
 */
int rt1718s_set_frs_enable(int port, int enable);

/**
 * Board override for fast role swap.
 *
 * @param port		USB-C port
 * @param enable	enable/disable FRS
 * @return EC_SUCCESS if success, EC_ERROR_UNKNOWN otherwise.
 */
__override_proto int board_rt1718s_set_frs_enable(int port, int enable);

/**
 * Software reset RT1718S
 *
 * @param port		USB-C port
 * @return EC_SUCCESS if success, EC_ERROR_UNKNOWN otherwise.
 */
int rt1718s_sw_reset(int port);

/**
 * Board hook for rt1718s_set_snk_enable
 *
 * @param port		USB-C port
 * @param enable	enable/disable sink
 * @return EC_SUCCESS if success, EC_ERROR_UNKNOWN otherwise.
 */
__override_proto int board_rt1718s_set_snk_enable(int port, int enable);

/**
 * Board hook for rt1718s_set_src_enable
 *
 * @param port		USB-C port
 * @param enable	enable/disable source
 * @return EC_SUCCESS if success, EC_ERROR_UNKNOWN otherwise.
 */
__override_proto int board_rt1718s_set_src_enable(int port, int enable);
#endif /* __CROS_EC_USB_PD_TCPM_MT6370_H */
