/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


/*
 * IDT P9221-R7 Wireless Power Receiver driver definitions.
 */

#ifndef __P9221_R7_H
#define __P9221_R7_H

#include "common.h"
#include "gpio.h"
#include "charge_manager.h"
#include "task.h"


/* ========== Variant-specific configuration ============ */

#define P9221_R7_ADDR_FLAGS			0x61

/*
 * P9221 common registers
 */
#define P9221_CHIP_ID_REG			0x00
#define P9221_CHIP_ID				0x9220
#define P9221_CHIP_REVISION_REG			0x02
#define P9221_CUSTOMER_ID_REG			0x03
#define P9221R7_CUSTOMER_ID_VAL			0x05
#define P9221_OTP_FW_MAJOR_REV_REG		0x04
#define P9221_OTP_FW_MINOR_REV_REG		0x06
#define P9221_OTP_FW_DATE_REG			0x08
#define P9221_OTP_FW_DATE_SIZE			12
#define P9221_OTP_FW_TIME_REG			0x14
#define P9221_OTP_FW_TIME_SIZE			8
#define P9221_SRAM_FW_MAJOR_REV_REG		0x1C
#define P9221_SRAM_FW_MINOR_REV_REG		0x1E
#define P9221_SRAM_FW_DATE_REG			0x20
#define P9221_SRAM_FW_DATE_SIZE			12
#define P9221_SRAM_FW_TIME_REG			0x2C
#define P9221_SRAM_FW_TIME_SIZE			8
#define P9221_STATUS_REG			0x34
#define P9221_INT_REG				0x36
#define P9221_INT_MASK				0xF7
#define P9221_INT_ENABLE_REG			0x38
#define P9221_GPP_TX_MF_ID			0x0072

/*
 * P9221 Rx registers (x != 5)
 */
#define P9221_CHARGE_STAT_REG			0x3A
#define P9221_EPT_REG				0x3B
#define P9221_VOUT_ADC_REG			0x3C
#define P9221_VOUT_ADC_MASK			0x0FFF
#define P9221_VOUT_SET_REG			0x3E
#define P9221_MAX_VOUT_SET_MV_DEFAULT		9000
#define P9221_VRECT_ADC_REG			0x40
#define P9221_VRECT_ADC_MASK			0x0FFF
#define P9221_OVSET_REG				0x42
#define P9221_OVSET_MASK			0x70
#define P9221_OVSET_SHIFT			4
#define P9221_RX_IOUT_REG			0x44
#define P9221_DIE_TEMP_ADC_REG			0x46
#define P9221_DIE_TEMP_ADC_MASK			0x0FFF
#define P9221_OP_FREQ_REG			0x48
#define P9221_ILIM_SET_REG			0x4A
#define P9221_ALIGN_X_ADC_REG			0x4B
#define P9221_ALIGN_Y_ADC_REG			0x4C
#define P9221_OP_MODE_REG			0x4D
#define P9221_COM_REG				0x4E
#define P9221_FW_SWITCH_KEY_REG			0x4F
#define P9221_INT_CLEAR_REG			0x56
#define P9221_RXID_REG				0x5C
#define P9221_RXID_LEN				6
#define P9221_MPREQ_REG				0x5C
#define P9221_MPREQ_LEN				6
#define P9221_FOD_REG				0x68
#define P9221_NUM_FOD				16
#define P9221_RX_RAWIOUT_REG			0x7A
#define P9221_RX_RAWIOUT_MASK			0xFFF
#define P9221_PMA_AD_REG			0x7C
#define P9221_RX_PINGFREQ_REG			0xFC
#define P9221_RX_PINGFREQ_MASK			0xFFF
#define P9221_LAST_REG				0xFF

/*
 * P9221R7 unique registers
 */
#define P9221R7_INT_CLEAR_REG			0x3A
#define P9221R7_VOUT_SET_REG			0x3C
#define P9221R7_ILIM_SET_REG			0x3D
#define P9221R7_ILIM_SET_MAX			0x0E	/* 0x0E = 1.6A */
#define P9221R7_CHARGE_STAT_REG			0x3E
#define P9221R7_EPT_REG				0x3F
#define P9221R7_VRECT_REG			0x40
#define P9221R7_VOUT_REG			0x42
#define P9221R7_IOUT_REG			0x44
#define P9221R7_OP_FREQ_REG			0x48
#define P9221R7_SYSTEM_MODE_REG			0x4C
#define P9221R7_COM_CHAN_RESET_REG		0x50
#define P9221R7_COM_CHAN_SEND_SIZE_REG		0x58
#define P9221R7_COM_CHAN_SEND_IDX_REG		0x59
#define P9221R7_COM_CHAN_RECV_SIZE_REG		0x5A
#define P9221R7_COM_CHAN_RECV_IDX_REG		0x5B
#define P9221R7_VRECT_ADC_REG			0x60
#define P9221R7_VOUT_ADC_REG			0x62
#define P9221R7_VOUT_ADC_MASK			0xFFF
#define P9221R7_IOUT_ADC_REG			0x64
#define P9221R7_IOUT_ADC_MASK			0xFFF
#define P9221R7_DIE_TEMP_ADC_REG		0x66
#define P9221R7_DIE_TEMP_ADC_MASK		0xFFF
#define P9221R7_AC_PERIOD_REG			0x68
#define P9221R7_TX_PINGFREQ_REG			0x6A
#define P9221R7_EXT_TEMP_REG			0x6C
#define P9221R7_EXT_TEMP_MASK			0xFFF
#define P9221R7_FOD_REG				0x70
#define P9221R7_NUM_FOD				16
#define P9221R7_DEBUG_REG			0x80
#define P9221R7_EPP_Q_FACTOR_REG		0x83
#define P9221R7_EPP_TX_GUARANTEED_POWER_REG	0x84
#define P9221R7_EPP_TX_POTENTIAL_POWER_REG	0x85
#define P9221R7_EPP_TX_CAPABILITY_FLAGS_REG	0x86
#define P9221R7_EPP_RENEGOTIATION_REG		0x87
#define P9221R7_EPP_CUR_RPP_HEADER_REG		0x88
#define P9221R7_EPP_CUR_NEGOTIATED_POWER_REG	0x89
#define P9221R7_EPP_CUR_MAXIMUM_POWER_REG	0x8A
#define P9221R7_EPP_CUR_FSK_MODULATION_REG	0x8B
#define P9221R7_EPP_REQ_RPP_HEADER_REG		0x8C
#define P9221R7_EPP_REQ_NEGOTIATED_POWER_REG	0x8D
#define P9221R7_EPP_REQ_MAXIMUM_POWER_REG	0x8E
#define P9221R7_EPP_REQ_FSK_MODULATION_REG	0x8F
#define P9221R7_VRECT_TARGET_REG		0x90
#define P9221R7_VRECT_KNEE_REG			0x92
#define P9221R7_VRECT_CORRECTION_FACTOR_REG	0x93
#define P9221R7_VRECT_MAX_CORRECTION_FACTOR_REG	0x94
#define P9221R7_VRECT_MIN_CORRECTION_FACTOR_REG	0x96
#define P9221R7_FOD_SECTION_REG			0x99
#define P9221R7_VRECT_ADJ_REG			0x9E
#define P9221R7_ALIGN_X_ADC_REG			0xA0
#define P9221R7_ALIGN_Y_ADC_REG			0xA1
#define P9221R7_ASK_MODULATION_DEPTH_REG	0xA2
#define P9221R7_OVSET_REG			0xA3
#define P9221R7_OVSET_MASK			0x7
#define P9221R7_EPP_TX_SPEC_REV_REG		0xA9
#define P9221R7_EPP_TX_MFG_CODE_REG		0xAA
#define P9221R7_GP0_RESET_VOLT_REG		0xAC
#define P9221R7_GP1_RESET_VOLT_REG		0xAE
#define P9221R7_GP2_RESET_VOLT_REG		0xB0
#define P9221R7_GP3_RESET_VOLT_REG		0xB2
#define P9221R7_PROP_TX_ID_REG			0xB4
#define P9221R7_PROP_TX_ID_SIZE			4
#define P9221R7_DATA_SEND_BUF_START		0x100
#define P9221R7_DATA_SEND_BUF_SIZE		0x80
#define P9221R7_DATA_RECV_BUF_START		0x180
#define P9221R7_DATA_RECV_BUF_SIZE		0x80
#define P9221R7_MAX_PP_BUF_SIZE			16
#define P9221R7_LAST_REG			0x1FF

/*
 * System Mode Mask (r7+/0x4C)
 */
#define P9221R7_SYSTEM_MODE_EXTENDED_MASK	(1 << 3)

/*
 * TX ID GPP Mask (r7+/0xB4->0xB7)
 */
#define P9221R7_PROP_TX_ID_GPP_MASK		(1 << 29)

/*
 * Com Channel Commands
 */
#define P9221R7_COM_CHAN_CCRESET		BIT(7)
#define P9221_COM_CHAN_RETRIES			5

/*
 * End of Power packet types
 */
#define P9221_EOP_UNKNOWN			0x00
#define P9221_EOP_EOC				0x01
#define P9221_EOP_INTERNAL_FAULT		0x02
#define P9221_EOP_OVER_TEMP			0x03
#define P9221_EOP_OVER_VOLT			0x04
#define P9221_EOP_OVER_CURRENT			0x05
#define P9221_EOP_BATT_FAIL			0x06
#define P9221_EOP_RECONFIG			0x07
#define P9221_EOP_NO_RESPONSE			0x08
#define P9221_EOP_NEGOTIATION_FAIL		0x0A
#define P9221_EOP_RESTART_POWER			0x0B

/*
 * Command flags
 */
#define P9221R7_COM_RENEGOTIATE			P9221_COM_RENEGOTIATE
#define P9221R7_COM_SWITCH2RAM			P9221_COM_SWITCH_TO_RAM_MASK
#define P9221R7_COM_CLRINT			P9221_COM_CLEAR_INT_MASK
#define P9221R7_COM_SENDCSP			P9221_COM_SEND_CHG_STAT_MASK
#define P9221R7_COM_SENDEPT			P9221_COM_SEND_EOP_MASK
#define P9221R7_COM_LDOTGL			P9221_COM_LDO_TOGGLE
#define P9221R7_COM_CCACTIVATE			BIT(0)

#define P9221_COM_RENEGOTIATE			BIT(7)
#define P9221_COM_SWITCH_TO_RAM_MASK		BIT(6)
#define P9221_COM_CLEAR_INT_MASK		BIT(5)
#define P9221_COM_SEND_CHG_STAT_MASK		BIT(4)
#define P9221_COM_SEND_EOP_MASK			BIT(3)
#define P9221_COM_LDO_TOGGLE			BIT(1)

/*
 * Interrupt/Status flags for P9221
 */
#define P9221_STAT_VOUT				BIT(7)
#define P9221_STAT_VRECT			BIT(6)
#define P9221_STAT_ACMISSING			BIT(5)
#define P9221_STAT_OV_TEMP			BIT(2)
#define P9221_STAT_OV_VOLT			BIT(1)
#define P9221_STAT_OV_CURRENT			BIT(0)
#define P9221_STAT_LIMIT_MASK			(P9221_STAT_OV_TEMP | \
						 P9221_STAT_OV_VOLT | \
						 P9221_STAT_OV_CURRENT)
/*
 * Interrupt/Status flags for P9221R7
 */
#define P9221R7_STAT_CCRESET			BIT(12)
#define P9221R7_STAT_CCERROR			BIT(11)
#define P9221R7_STAT_PPRCVD			BIT(10)
#define P9221R7_STAT_CCDATARCVD			BIT(9)
#define P9221R7_STAT_CCSENDBUSY			BIT(8)
#define P9221R7_STAT_VOUTCHANGED		BIT(7)
#define P9221R7_STAT_VRECTON			BIT(6)
#define P9221R7_STAT_MODECHANGED		BIT(5)
#define P9221R7_STAT_UV				BIT(3)
#define P9221R7_STAT_OVT			BIT(2)
#define P9221R7_STAT_OVV			BIT(1)
#define P9221R7_STAT_OVC			BIT(0)
#define P9221R7_STAT_MASK			0x1FFF
#define P9221R7_STAT_CC_MASK			(P9221R7_STAT_CCRESET | \
						 P9221R7_STAT_PPRCVD | \
						 P9221R7_STAT_CCERROR | \
						 P9221R7_STAT_CCDATARCVD | \
						 P9221R7_STAT_CCSENDBUSY)
#define P9221R7_STAT_LIMIT_MASK			(P9221R7_STAT_UV | \
						 P9221R7_STAT_OVV | \
						 P9221R7_STAT_OVT | \
						 P9221R7_STAT_OVC)

#define P9221_DC_ICL_BPP_MA			1000
#define P9221_DC_ICL_EPP_MA			1100
#define P9221_DC_IVL_BPP_MV			5000
#define P9221_DC_IVL_EPP_MV			9000
#define P9221_EPP_THRESHOLD_UV			7000000

#define true    1
#define false   0

struct wpc_charger_info {
	uint8_t online;				 /* wpc is online */
	uint8_t cust_id;			 /* customer id */
	uint8_t i2c_port;			 /* i2c port */
	/* Proprietary Packets receive buffer, to get Proprietary data from TX*/
	uint8_t pp_buf[P9221R7_MAX_PP_BUF_SIZE];
	uint8_t pp_buf_valid;
	/* Common message Packets receive buffer, for get data from TX */
	uint8_t rx_buf[P9221R7_DATA_RECV_BUF_SIZE];
	uint8_t rx_len;
	uint8_t rx_done;
	/* Message packets send buffer, used when send messages from RX to TX*/
	uint8_t tx_buf[P9221R7_DATA_SEND_BUF_SIZE];
	uint8_t tx_id;				/* TX device id */
	uint8_t tx_len;	 /* The data size need send to TX */
	uint8_t tx_done; /* TX data send has done */
	uint8_t tx_busy; /* when tx_busy=1, can't transfer data from RX to TX */
	/* p9221_check_vbus=1 when VBUS has changed, need update charge state */
	uint8_t p9221_check_vbus;
	/* p9221_check_det=1 when TX device has detected */
	uint8_t p9221_check_det;
	/* vbus_status is 1 when VBUS attached and is 0 when VBUS detached*/
	uint8_t vbus_status;
	/* supplier type of wireless charger */
	uint8_t charge_supplier;
	/* lock of send command to p9221 */
	struct mutex cmd_lock;
};

/* Interrupt handler for p9221 */
void p9221_interrupt(enum gpio_signal signal);

/**
 * notify p9221 detect update charger status when VBUS changed
 *
 * @param vbus: new status of VBUS, 1 if VBUS on, 0 if VBUS off.
 */
void p9221_notify_vbus_change(int vbus);

/**
 * get the fod (foreign-object detection) parameters for bpp charger type
 *
 * @param fod: return the real value of fod paramerters,
 *             return NULL if fod paramerters not set.
 *
 * @return the count bytes of fod paramerters.
 */
int board_get_fod(uint8_t **fod);

/**
 * get the fod (foreign-object detection) parameters for epp chager type
 *
 * @param fod: return the real value of fod paramerters,
 *             return NULL if fod paramerters not set.
 *
 * @return the count bytes of fod paramerters.
 */
int board_get_epp_fod(uint8_t **fod);

/**
 * return the wireless charge online status
 *
 * @return true if online, false if offline.
 */
int wpc_chip_is_online(void);

#endif
