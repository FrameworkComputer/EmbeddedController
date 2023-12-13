/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_TCPCI_H
#define __CROS_EC_USB_PD_TCPM_TCPCI_H

#include "config.h"
#include "ec_commands.h"
#include "tcpm/tcpm.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"

#define TCPC_REG_VENDOR_ID 0x0
#define TCPC_REG_PRODUCT_ID 0x2
#define TCPC_REG_BCD_DEV 0x4
#define TCPC_REG_TC_REV 0x6
#define TCPC_REG_PD_REV 0x8
#define TCPC_REG_PD_INT_REV 0xa

#define TCPC_REG_PD_INT_REV_REV_MASK 0xff00
#define TCPC_REG_PD_INT_REV_REV_1_0 0x10
#define TCPC_REG_PD_INT_REV_REV_2_0 0x20
#define TCPC_REG_PD_INT_REV_VER_MASK 0x00ff
#define TCPC_REG_PD_INT_REV_VER_1_0 0x10
#define TCPC_REG_PD_INT_REV_VER_1_1 0x11
#define TCPC_REG_PD_INT_REV_REV(reg) ((reg & TCPC_REG_PD_INT_REV_REV_MASK) >> 8)
#define TCPC_REG_PD_INT_REV_VER(reg) (reg & TCPC_REG_PD_INT_REV_VER_MASK)

#define TCPC_REG_ALERT 0x10
#define TCPC_REG_ALERT_NONE 0x0000
#define TCPC_REG_ALERT_MASK_ALL 0xffff
#define TCPC_REG_ALERT_VENDOR_DEF BIT(15)
#define TCPC_REG_ALERT_ALERT_EXT BIT(14)
#define TCPC_REG_ALERT_EXT_STATUS BIT(13)
#define TCPC_REG_ALERT_RX_BEGINNING BIT(12)
#define TCPC_REG_ALERT_VBUS_DISCNCT BIT(11)
#define TCPC_REG_ALERT_RX_BUF_OVF BIT(10)
#define TCPC_REG_ALERT_FAULT BIT(9)
#define TCPC_REG_ALERT_V_ALARM_LO BIT(8)
#define TCPC_REG_ALERT_V_ALARM_HI BIT(7)
#define TCPC_REG_ALERT_TX_SUCCESS BIT(6)
#define TCPC_REG_ALERT_TX_DISCARDED BIT(5)
#define TCPC_REG_ALERT_TX_FAILED BIT(4)
#define TCPC_REG_ALERT_RX_HARD_RST BIT(3)
#define TCPC_REG_ALERT_RX_STATUS BIT(2)
#define TCPC_REG_ALERT_POWER_STATUS BIT(1)
#define TCPC_REG_ALERT_CC_STATUS BIT(0)
#define TCPC_REG_ALERT_TX_COMPLETE                                 \
	(TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_DISCARDED | \
	 TCPC_REG_ALERT_TX_FAILED)

#define TCPC_REG_ALERT_MASK 0x12
#define TCPC_REG_ALERT_MASK_VENDOR_DEF BIT(15)

#define TCPC_REG_POWER_STATUS_MASK 0x14
#define TCPC_REG_FAULT_STATUS_MASK 0x15
#define TCPC_REG_EXT_STATUS_MASK 0x16
#define TCPC_REG_ALERT_EXTENDED_MASK 0x17

#define TCPC_REG_CONFIG_STD_OUTPUT 0x18
#define TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N BIT(6)
#define TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N BIT(5)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK (3 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_NONE (0 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB BIT(2)
#define TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP (2 << 2)
#define TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED BIT(0)

#define TCPC_REG_TCPC_CTRL 0x19
#define TCPC_REG_TCPC_CTRL_SET(polarity) (polarity)
#define TCPC_REG_TCPC_CTRL_POLARITY(reg) ((reg) & 0x1)
/*
 * In TCPCI Rev 2.0, this bit must be set this to generate CC status alerts when
 * a connection is found.
 */
#define TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT BIT(6)
#define TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL BIT(4)
#define TCPC_REG_TCPC_CTRL_BIST_TEST_MODE BIT(1)

#define TCPC_REG_ROLE_CTRL 0x1a
#define TCPC_REG_ROLE_CTRL_DRP_MASK BIT(6)
#define TCPC_REG_ROLE_CTRL_RP_MASK (BIT(5) | BIT(4))
#define TCPC_REG_ROLE_CTRL_CC2_MASK (BIT(3) | BIT(2))
#define TCPC_REG_ROLE_CTRL_CC1_MASK (BIT(1) | BIT(0))
#define TCPC_REG_ROLE_CTRL_SET(drp, rp, cc1, cc2)       \
	((((drp) << 6) & TCPC_REG_ROLE_CTRL_DRP_MASK) | \
	 (((rp) << 4) & TCPC_REG_ROLE_CTRL_RP_MASK) |   \
	 (((cc2) << 2) & TCPC_REG_ROLE_CTRL_CC2_MASK) | \
	 ((cc1) & TCPC_REG_ROLE_CTRL_CC1_MASK))
#define TCPC_REG_ROLE_CTRL_DRP(reg) (((reg) & TCPC_REG_ROLE_CTRL_DRP_MASK) >> 6)
#define TCPC_REG_ROLE_CTRL_RP(reg) (((reg) & TCPC_REG_ROLE_CTRL_RP_MASK) >> 4)
#define TCPC_REG_ROLE_CTRL_CC2(reg) (((reg) & TCPC_REG_ROLE_CTRL_CC2_MASK) >> 2)
#define TCPC_REG_ROLE_CTRL_CC1(reg) ((reg) & TCPC_REG_ROLE_CTRL_CC1_MASK)

#define TCPC_REG_FAULT_CTRL 0x1b
#define TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS BIT(1)
#define TCPC_REG_FAULT_CTRL_VCONN_OCP_FAULT_DIS BIT(0)

#define TCPC_REG_POWER_CTRL 0x1c
#define TCPC_REG_POWER_CTRL_FRS_ENABLE BIT(7)
#define TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS BIT(6)
#define TCPC_REG_POWER_CTRL_VOLT_ALARM_DIS BIT(5)
#define TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT BIT(4)
#define TCPC_REG_POWER_CTRL_FORCE_DISCHARGE BIT(2)
#define TCPC_REG_POWER_CTRL_SET(vconn) (vconn)
#define TCPC_REG_POWER_CTRL_VCONN(reg) ((reg) & 0x1)

#define TCPC_REG_CC_STATUS 0x1d
#define TCPC_REG_CC_STATUS_LOOK4CONNECTION_MASK BIT(5)
#define TCPC_REG_CC_STATUS_CONNECT_RESULT_MASK BIT(4)
#define TCPC_REG_CC_STATUS_CC2_STATE_MASK (BIT(3) | BIT(2))
#define TCPC_REG_CC_STATUS_CC1_STATE_MASK (BIT(1) | BIT(0))
#define TCPC_REG_CC_STATUS_SET(term, cc1, cc2) \
	((term) << 4 | ((cc2) & 0x3) << 2 | ((cc1) & 0x3))
#define TCPC_REG_CC_STATUS_LOOK4CONNECTION(reg) \
	((reg & TCPC_REG_CC_STATUS_LOOK4CONNECTION_MASK) >> 5)
#define TCPC_REG_CC_STATUS_TERM(reg) \
	(((reg) & TCPC_REG_CC_STATUS_CONNECT_RESULT_MASK) >> 4)
#define TCPC_REG_CC_STATUS_CC2(reg) \
	(((reg) & TCPC_REG_CC_STATUS_CC2_STATE_MASK) >> 2)
#define TCPC_REG_CC_STATUS_CC1(reg) ((reg) & TCPC_REG_CC_STATUS_CC1_STATE_MASK)

#define TCPC_REG_POWER_STATUS 0x1e
#define TCPC_REG_POWER_STATUS_MASK_ALL 0xff
#define TCPC_REG_POWER_STATUS_DEBUG_ACC_CON BIT(7)
#define TCPC_REG_POWER_STATUS_UNINIT BIT(6)
#define TCPC_REG_POWER_STATUS_SOURCING_VBUS BIT(4)
#define TCPC_REG_POWER_STATUS_VBUS_DET BIT(3)
#define TCPC_REG_POWER_STATUS_VBUS_PRES BIT(2)
#define TCPC_REG_POWER_STATUS_SINKING_VBUS BIT(0)

#define TCPC_REG_FAULT_STATUS 0x1f
#define TCPC_REG_FAULT_STATUS_ALL_REGS_RESET BIT(7)
#define TCPC_REG_FAULT_STATUS_FORCE_OFF_VBUS BIT(6)
#define TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL BIT(5)
#define TCPC_REG_FAULT_STATUS_FORCE_DISCHARGE_FAIL BIT(4)
#define TCPC_REG_FAULT_STATUS_VBUS_OVER_CURRENT BIT(3)
#define TCPC_REG_FAULT_STATUS_VBUS_OVER_VOLTAGE BIT(2)
#define TCPC_REG_FAULT_STATUS_VCONN_OVER_CURRENT BIT(1)
#define TCPC_REG_FAULT_STATUS_I2C_INTERFACE_ERR BIT(0)

#define TCPC_REG_EXT_STATUS 0x20
#define TCPC_REG_EXT_STATUS_SAFE0V BIT(0)

#define TCPC_REG_ALERT_EXT 0x21
#define TCPC_REG_ALERT_EXT_TIMER_EXPIRED BIT(2)
#define TCPC_REG_ALERT_EXT_SRC_FRS BIT(1)
#define TCPC_REG_ALERT_EXT_SNK_FRS BIT(0)

#define TCPC_REG_COMMAND 0x23
#define TCPC_REG_COMMAND_WAKE_I2C 0x11
#define TCPC_REG_COMMAND_DISABLE_VBUS_DETECT 0x22
#define TCPC_REG_COMMAND_ENABLE_VBUS_DETECT 0x33
#define TCPC_REG_COMMAND_SNK_CTRL_LOW 0x44
#define TCPC_REG_COMMAND_SNK_CTRL_HIGH 0x55
#define TCPC_REG_COMMAND_SRC_CTRL_LOW 0x66
#define TCPC_REG_COMMAND_SRC_CTRL_HIGH 0x77
#define TCPC_REG_COMMAND_LOOK4CONNECTION 0x99
#define TCPC_REG_COMMAND_RESET_TRANSMIT_BUF 0xDD
#define TCPC_REG_COMMAND_RESET_RECEIVE_BUF 0xEE
#define TCPC_REG_COMMAND_I2CIDLE 0xFF

#define TCPC_REG_DEV_CAP_1 0x24
#define TCPC_REG_DEV_CAP_1_VBUS_NONDEFAULT_TARGET BIT(15)
#define TCPC_REG_DEV_CAP_1_VBUS_OCP_REPORTING BIT(14)
#define TCPC_REG_DEV_CAP_1_VBUS_OVP_REPORTING BIT(13)
#define TCPC_REG_DEV_CAP_1_BLEED_DISCHARGE BIT(12)
#define TCPC_REG_DEV_CAP_1_FORCE_DISCHARGE BIT(11)
#define TCPC_REG_DEV_CAP_1_VBUS_MEASURE_ALARM_CAPABLE BIT(10)
#define TCPC_REG_DEV_CAP_1_SRC_RESISTOR_MASK (BIT(8) | BIT(9))
#define TCPC_REG_DEV_CAP_1_SRC_RESISTOR_RP_DEF (0 << 8)
#define TCPC_REG_DEV_CAP_1_SRC_RESISTOR_RP_1P5_DEF (1 << 8)
#define TCPC_REG_DEV_CAP_1_SRC_RESISTOR_RP_3P0_1P5_DEF (2 << 8)
#define TCPC_REG_DEV_CAP_1_PWRROLE_MASK (BIT(5) | BIT(6) | BIT(7))
#define TCPC_REG_DEV_CAP_1_PWRROLE_SRC_OR_SNK (0 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_SRC (1 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_SNK (2 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_SNK_ACC (3 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_DRP (4 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP_ADPT_CBL (5 << 5)
#define TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP (6 << 5)
#define TCPC_REG_DEV_CAP_1_ALL_SOP_STAR_MSGS_SUPPORTED BIT(4)
#define TCPC_REG_DEV_CAP_1_SOURCE_VCONN BIT(3)
#define TCPC_REG_DEV_CAP_1_SINK_VBUS BIT(2)
#define TCPC_REG_DEV_CAP_1_SOURCE_NONDEFAULT_VBUS BIT(1)
#define TCPC_REG_DEV_CAP_1_SOURCE_VBUS BIT(0)

#define TCPC_REG_DEV_CAP_2 0x26
#define TCPC_REG_DEV_CAP_2_LONG_MSG BIT(12)
#define TCPC_REG_DEV_CAP_2_SNK_FR_SWAP BIT(9)

#define TCPC_REG_STD_INPUT_CAP 0x28
#define TCPC_REG_STD_INPUT_CAP_SRC_FR_SWAP (BIT(4) | BIT(3))
#define TCPC_REG_STD_INPUT_CAP_EXT_OVR_V_F BIT(2)
#define TCPC_REG_STD_INPUT_CAP_EXT_OVR_C_F BIT(1)
#define TCPC_REG_STD_INPUT_CAP_FORCE_OFF_VBUS BIT(0)

#define TCPC_REG_STD_OUTPUT_CAP 0x29
#define TCPC_REG_STD_OUTPUT_CAP_SNK_DISC_DET BIT(7)
#define TCPC_REG_STD_OUTPUT_CAP_DBG_ACCESSORY BIT(6)
#define TCPC_REG_STD_OUTPUT_CAP_VBUS_PRESENT_MON BIT(5)
#define TCPC_REG_STD_OUTPUT_CAP_AUDIO_ACCESSORY BIT(4)
#define TCPC_REG_STD_OUTPUT_CAP_ACTIVE_CABLE BIT(3)
#define TCPC_REG_STD_OUTPUT_CAP_MUX_CONF_CTRL BIT(2)
#define TCPC_REG_STD_OUTPUT_CAP_CONN_PRESENT BIT(1)
#define TCPC_REG_STD_OUTPUT_CAP_CONN_ORIENTATION BIT(0)

#define TCPC_REG_CONFIG_EXT_1 0x2A
#define TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR BIT(1)

#define TCPC_REG_GENERIC_TIMER 0x2c

#define TCPC_REG_MSG_HDR_INFO 0x2e
#define TCPC_REG_MSG_HDR_INFO_SET(drole, prole) \
	((drole) << 3 | (PD_REV20 << 1) | (prole))
#define TCPC_REG_MSG_HDR_INFO_DROLE(reg) (((reg) & 0x8) >> 3)
#define TCPC_REG_MSG_HDR_INFO_PROLE(reg) ((reg) & 0x1)

#define TCPC_REG_RX_DETECT 0x2f
#define TCPC_REG_RX_DETECT_MSG_DISABLE_DISCONNECT BIT(7)
#define TCPC_REG_RX_DETECT_CABLE_RST BIT(6)
#define TCPC_REG_RX_DETECT_HRST BIT(5)
#define TCPC_REG_RX_DETECT_SOPPP_DBG BIT(4)
#define TCPC_REG_RX_DETECT_SOPP_DBG BIT(3)
#define TCPC_REG_RX_DETECT_SOPPP BIT(2)
#define TCPC_REG_RX_DETECT_SOPP BIT(1)
#define TCPC_REG_RX_DETECT_SOP BIT(0)
#define TCPC_REG_RX_DETECT_SOP_HRST_MASK \
	(TCPC_REG_RX_DETECT_SOP | TCPC_REG_RX_DETECT_HRST)
#define TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK         \
	(TCPC_REG_RX_DETECT_SOP | TCPC_REG_RX_DETECT_SOPP | \
	 TCPC_REG_RX_DETECT_SOPPP | TCPC_REG_RX_DETECT_HRST)
#define TCPC_REG_RX_DETECT_NONE 0xff

/* TCPCI Rev 1.0 receive registers */
#define TCPC_REG_RX_BYTE_CNT 0x30
#define TCPC_REG_RX_BUF_FRAME_TYPE 0x31
#define TCPC_REG_RX_HDR 0x32
#define TCPC_REG_RX_DATA 0x34 /* through 0x4f */

/*
 * In TCPCI Rev 2.0, the RECEIVE_BUFFER is comprised of three sets of registers:
 * READABLE_BYTE_COUNT, RX_BUF_FRAME_TYPE and RX_BUF_BYTE_x. These registers can
 * only be accessed by reading at a common register address 30h.
 */
#define TCPC_REG_RX_BUFFER 0x30

#define TCPC_REG_TRANSMIT 0x50
#define TCPC_REG_TRANSMIT_SET_WITH_RETRY(retries, type) \
	((retries) << 4 | (type))
#define TCPC_REG_TRANSMIT_SET_WITHOUT_RETRY(type) (type)
#define TCPC_REG_TRANSMIT_RETRY(reg) (((reg) & 0x30) >> 4)
#define TCPC_REG_TRANSMIT_TYPE(reg) ((reg) & 0x7)

/* TCPCI Rev 1.0 transmit registers */
#define TCPC_REG_TX_BYTE_CNT 0x51
#define TCPC_REG_TX_HDR 0x52
#define TCPC_REG_TX_DATA 0x54 /* through 0x6f */

/*
 * In TCPCI Rev 2.0, the TRANSMIT_BUFFER holds the I2C_WRITE_BYTE_COUNT and the
 * portion of the SOP* USB PD message payload (including the header and/or the
 * data bytes) most recently written by the TCPM in TX_BUF_BYTE_x. TX_BUF_BYTE_x
 * is “hidden” and can only be accessed by writing to register address 51h
 */
#define TCPC_REG_TX_BUFFER 0x51

#define TCPC_REG_VBUS_VOLTAGE 0x70
#define TCPC_REG_VBUS_VOLTAGE_MEASUREMENT GENMASK(9, 0)
#define TCPC_REG_VBUS_VOLTAGE_SCALE_FACTOR GENMASK(11, 10)
#define TCPC_REG_VBUS_VOLTAGE_LSB 25

/*
 * 00: the measurement is not scaled
 * 01: the measurement is divided by 2
 * 10: the measurement is divided by 4
 * 11: reserved
 */
#define TCPC_REG_VBUS_VOLTAGE_SCALE(x) \
	(1 << (((x) & TCPC_REG_VBUS_VOLTAGE_SCALE_FACTOR) >> 10))
#define TCPC_REG_VBUS_VOLTAGE_MEASURE(x) \
	((x) & TCPC_REG_VBUS_VOLTAGE_MEASUREMENT)
#define TCPC_REG_VBUS_VOLTAGE_VBUS(x)                                        \
	(TCPC_REG_VBUS_VOLTAGE_SCALE(x) * TCPC_REG_VBUS_VOLTAGE_MEASURE(x) * \
	 TCPC_REG_VBUS_VOLTAGE_LSB)

#define TCPC_REG_VBUS_SINK_DISCONNECT_THRESH 0x72
#define TCPC_REG_VBUS_SINK_DISCONNECT_THRESH_DEFAULT 0x008C /* 3.5 V */

#define TCPC_REG_VBUS_STOP_DISCHARGE_THRESH 0x74
#define TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG 0x76
#define TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG 0x78

#define TCPC_REG_VBUS_NONDEFAULT_TARGET 0x7a

extern const struct tcpm_drv tcpci_tcpm_drv;
extern const struct usb_mux_driver tcpci_tcpm_usb_mux_driver;

void tcpci_set_cached_rp(int port, int rp);
int tcpci_get_cached_rp(int port);
void tcpci_set_cached_pull(int port, enum tcpc_cc_pull pull);
enum tcpc_cc_pull tcpci_get_cached_pull(int port);

void tcpci_tcpc_alert(int port);
int tcpci_tcpm_init(int port);
int tcpci_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
		      enum tcpc_cc_voltage_status *cc2);
bool tcpci_tcpm_check_vbus_level(int port, enum vbus_level level);
int tcpci_tcpm_select_rp_value(int port, int rp);
int tcpci_tcpm_set_cc(int port, int pull);
int tcpci_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity);
int tcpci_tcpm_sop_prime_enable(int port, bool enable);
int tcpci_tcpm_set_vconn(int port, int enable);
int tcpci_tcpm_set_msg_header(int port, int power_role, int data_role);
int tcpci_tcpm_set_rx_enable(int port, int enable);
int tcpci_tcpm_get_message_raw(int port, uint32_t *payload, int *head);
int tcpci_tcpm_transmit(int port, enum tcpci_msg_type type, uint16_t header,
			const uint32_t *data);
int tcpci_tcpm_release(int port);
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
int tcpci_set_role_ctrl(int port, enum tcpc_drp drp, enum tcpc_rp_value rp,
			enum tcpc_cc_pull pull);
int tcpci_tcpc_drp_toggle(int port);
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
int tcpci_enter_low_power_mode(int port);
void tcpci_wake_low_power_mode(int port);
#endif
int tcpci_hard_reset_reinit(int port);

enum ec_error_list tcpci_set_bist_test_mode(const int port, const bool enable);
enum ec_error_list tcpci_get_bist_test_mode(const int port, bool *enable);
void tcpci_tcpc_discharge_vbus(int port, int enable);
void tcpci_tcpc_enable_auto_discharge_disconnect(int port, int enable);
int tcpci_tcpc_debug_accessory(int port, bool enable);

int tcpci_tcpm_mux_init(const struct usb_mux *me);
int tcpci_tcpm_mux_set(const struct usb_mux *me, mux_state_t mux_state,
		       bool *ack_required);
int tcpci_tcpm_mux_get(const struct usb_mux *me, mux_state_t *mux_state);
int tcpci_tcpm_mux_enter_low_power(const struct usb_mux *me);

/**
 * Get the TCPC chip information (chip IDs, etc) for the given port.
 *
 * The returned value is cached internally, so subsequent calls to this function
 * will not access the TCPC. If live is true, data will be fetched from the TCPC
 * regardless of whether any cached data is available.
 *
 * If chip_info is NULL, this will ensure the cache is up to date but avoid
 * writing the output chip_info.
 *
 * If the TCPC is accessed (live data is retrieved), this will wake the chip up
 * from low power mode on I2C access. It is expected that the USB-PD state
 * machine will return it to low power mode as appropriate afterward.
 *
 * Returns EC_SUCCESS or an error; chip_info is not updated on error.
 */
int tcpci_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 *chip_info);

/**
 * Equivalent to tcpci_get_chip_info, but allows the caller to modify the cache
 * if new data is fetched.
 *
 * If mutator is non-NULL and data is read from the TCPC (either because live
 * data is requested or nothing was cached), then it is called with a pointer
 * to the cached information for the port. It can then make changes to the
 * cached data (for example correcting IDs that are known to be reported
 * incorrectly by some chips, possibly requiring more communication with the
 * TCPC). Any error returned by mutator causes this function to return with the
 * same error.
 *
 * If mutator writes through the cached data pointer, those changes will be
 * retained until live data is requested again.
 */
int tcpci_get_chip_info_mutable(
	int port, int live, struct ec_response_pd_chip_info_v1 *chip_info,
	int (*mutator)(int port, bool live,
		       struct ec_response_pd_chip_info_v1 *cached));

/**
 * This function is identical to the tcpci_get_vbus_voltage without
 * checking the DEV_CAP_1.
 *
 * @param port: The USB-C port to query
 * @param vbus: VBUS voltage in mV the TCPC sensed
 *
 * @return EC_SUCCESS on success, and otherwise on failure.
 */
int tcpci_get_vbus_voltage_no_check(int port, int *vbus);
int tcpci_get_vbus_voltage(int port, int *vbus);
bool tcpci_tcpm_get_snk_ctrl(int port);
int tcpci_tcpm_set_snk_ctrl(int port, int enable);
bool tcpci_tcpm_get_src_ctrl(int port);
int tcpci_tcpm_set_src_ctrl(int port, int enable);

int tcpci_tcpc_fast_role_swap_enable(int port, int enable);

#endif /* __CROS_EC_USB_PD_TCPM_TCPCI_H */
