/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */
/* For Fairchild FUSB302 */
#ifndef __CROS_EC_DRIVER_TCPM_FUSB302_H
#define __CROS_EC_DRIVER_TCPM_FUSB302_H

/* Chip Device ID - 302A or 302B */
#define FUSB302_DEVID_302A 0x08
#define FUSB302_DEVID_302B 0x09

/* I2C slave address varies by part number */
/* FUSB302BUCX / FUSB302BMPX */
#define FUSB302_I2C_SLAVE_ADDR_FLAGS 0x22
/* FUSB302B01MPX */
#define FUSB302_I2C_SLAVE_ADDR_B01_FLAGS 0x23
/* FUSB302B10MPX */
#define FUSB302_I2C_SLAVE_ADDR_B10_FLAGS 0x24
/* FUSB302B11MPX */
#define FUSB302_I2C_SLAVE_ADDR_B11_FLAGS 0x25

#define TCPC_REG_DEVICE_ID	0x01

#define TCPC_REG_SWITCHES0	0x02
#define TCPC_REG_SWITCHES0_CC2_PU_EN	(1<<7)
#define TCPC_REG_SWITCHES0_CC1_PU_EN	(1<<6)
#define TCPC_REG_SWITCHES0_VCONN_CC2	(1<<5)
#define TCPC_REG_SWITCHES0_VCONN_CC1	(1<<4)
#define TCPC_REG_SWITCHES0_MEAS_CC2	(1<<3)
#define TCPC_REG_SWITCHES0_MEAS_CC1	(1<<2)
#define TCPC_REG_SWITCHES0_CC2_PD_EN	(1<<1)
#define TCPC_REG_SWITCHES0_CC1_PD_EN	(1<<0)

#define TCPC_REG_SWITCHES1	0x03
#define TCPC_REG_SWITCHES1_POWERROLE	(1<<7)
#define TCPC_REG_SWITCHES1_SPECREV1	(1<<6)
#define TCPC_REG_SWITCHES1_SPECREV0	(1<<5)
#define TCPC_REG_SWITCHES1_DATAROLE	(1<<4)
#define TCPC_REG_SWITCHES1_AUTO_GCRC	(1<<2)
#define TCPC_REG_SWITCHES1_TXCC2_EN	(1<<1)
#define TCPC_REG_SWITCHES1_TXCC1_EN	(1<<0)

#define TCPC_REG_MEASURE	0x04
#define TCPC_REG_MEASURE_MDAC_MASK	0x3F
#define TCPC_REG_MEASURE_VBUS		(1<<6)
/*
 * MDAC reference voltage step size is 42 mV. Round our thresholds to reduce
 * maximum error, which also matches suggested thresholds in datasheet
 * (Table 3. Host Interrupt Summary).
 */
#define TCPC_REG_MEASURE_MDAC_MV(mv)	(DIV_ROUND_NEAREST((mv), 42) & 0x3f)

#define TCPC_REG_CONTROL0	0x06
#define TCPC_REG_CONTROL0_TX_FLUSH	(1<<6)
#define TCPC_REG_CONTROL0_INT_MASK	(1<<5)
#define TCPC_REG_CONTROL0_HOST_CUR_MASK (3<<2)
#define TCPC_REG_CONTROL0_HOST_CUR_3A0  (3<<2)
#define TCPC_REG_CONTROL0_HOST_CUR_1A5  (2<<2)
#define TCPC_REG_CONTROL0_HOST_CUR_USB  (1<<2)
#define TCPC_REG_CONTROL0_TX_START	(1<<0)

#define TCPC_REG_CONTROL1	0x07
#define TCPC_REG_CONTROL1_ENSOP2DB	(1<<6)
#define TCPC_REG_CONTROL1_ENSOP1DB	(1<<5)
#define TCPC_REG_CONTROL1_BIST_MODE2	(1<<4)
#define TCPC_REG_CONTROL1_RX_FLUSH	(1<<2)
#define TCPC_REG_CONTROL1_ENSOP2	(1<<1)
#define TCPC_REG_CONTROL1_ENSOP1	(1<<0)

#define TCPC_REG_CONTROL2	0x08
/* two-bit field, valid values below */
#define TCPC_REG_CONTROL2_MODE_MASK	(0x3<<TCPC_REG_CONTROL2_MODE_POS)
#define TCPC_REG_CONTROL2_MODE_DFP	(0x3)
#define TCPC_REG_CONTROL2_MODE_UFP	(0x2)
#define TCPC_REG_CONTROL2_MODE_DRP	(0x1)
#define TCPC_REG_CONTROL2_MODE_POS	(1)
#define TCPC_REG_CONTROL2_TOGGLE	(1<<0)

#define TCPC_REG_CONTROL3	0x09
#define TCPC_REG_CONTROL3_SEND_HARDRESET	(1<<6)
#define TCPC_REG_CONTROL3_BIST_TMODE		(1<<5) /* 302B Only */
#define TCPC_REG_CONTROL3_AUTO_HARDRESET	(1<<4)
#define TCPC_REG_CONTROL3_AUTO_SOFTRESET	(1<<3)
/* two-bit field */
#define TCPC_REG_CONTROL3_N_RETRIES		(1<<1)
#define TCPC_REG_CONTROL3_N_RETRIES_POS		(1)
#define TCPC_REG_CONTROL3_N_RETRIES_SIZE	(2)
#define TCPC_REG_CONTROL3_AUTO_RETRY		(1<<0)

#define TCPC_REG_MASK		0x0A
#define TCPC_REG_MASK_VBUSOK		(1<<7)
#define TCPC_REG_MASK_ACTIVITY		(1<<6)
#define TCPC_REG_MASK_COMP_CHNG		(1<<5)
#define TCPC_REG_MASK_CRC_CHK		(1<<4)
#define TCPC_REG_MASK_ALERT		(1<<3)
#define TCPC_REG_MASK_WAKE		(1<<2)
#define TCPC_REG_MASK_COLLISION		(1<<1)
#define TCPC_REG_MASK_BC_LVL		(1<<0)

#define TCPC_REG_POWER		0x0B
#define TCPC_REG_POWER_PWR		(1<<0)	/* four-bit field */
#define TCPC_REG_POWER_PWR_LOW		0x1 /* Bandgap + Wake circuitry */
#define TCPC_REG_POWER_PWR_MEDIUM	0x3 /* LOW + Receiver + Current refs */
#define TCPC_REG_POWER_PWR_HIGH		0x7 /* MEDIUM + Measure block */
#define TCPC_REG_POWER_PWR_ALL		0xF /* HIGH + Internal Oscillator */

#define TCPC_REG_RESET		0x0C
#define TCPC_REG_RESET_PD_RESET		(1<<1)
#define TCPC_REG_RESET_SW_RESET		(1<<0)

#define TCPC_REG_MASKA		0x0E
#define TCPC_REG_MASKA_OCP_TEMP		(1<<7)
#define TCPC_REG_MASKA_TOGDONE		(1<<6)
#define TCPC_REG_MASKA_SOFTFAIL		(1<<5)
#define TCPC_REG_MASKA_RETRYFAIL	(1<<4)
#define TCPC_REG_MASKA_HARDSENT		(1<<3)
#define TCPC_REG_MASKA_TX_SUCCESS	(1<<2)
#define TCPC_REG_MASKA_SOFTRESET	(1<<1)
#define TCPC_REG_MASKA_HARDRESET	(1<<0)

#define TCPC_REG_MASKB		0x0F
#define TCPC_REG_MASKB_GCRCSENT		(1<<0)

#define TCPC_REG_STATUS0A	0x3C
#define TCPC_REG_STATUS0A_SOFTFAIL	(1<<5)
#define TCPC_REG_STATUS0A_RETRYFAIL	(1<<4)
#define TCPC_REG_STATUS0A_POWER		(1<<2) /* two-bit field */
#define TCPC_REG_STATUS0A_RX_SOFT_RESET	(1<<1)
#define TCPC_REG_STATUS0A_RX_HARD_RESEt	(1<<0)

#define TCPC_REG_STATUS1A	0x3D
/* three-bit field, valid values below */
#define TCPC_REG_STATUS1A_TOGSS		(1<<3)
#define TCPC_REG_STATUS1A_TOGSS_RUNNING		0x0
#define TCPC_REG_STATUS1A_TOGSS_SRC1		0x1
#define TCPC_REG_STATUS1A_TOGSS_SRC2		0x2
#define TCPC_REG_STATUS1A_TOGSS_SNK1		0x5
#define TCPC_REG_STATUS1A_TOGSS_SNK2		0x6
#define TCPC_REG_STATUS1A_TOGSS_AA		0x7
#define TCPC_REG_STATUS1A_TOGSS_POS		(3)
#define TCPC_REG_STATUS1A_TOGSS_MASK		(0x7)

#define TCPC_REG_STATUS1A_RXSOP2DB	(1<<2)
#define TCPC_REG_STATUS1A_RXSOP1DB	(1<<1)
#define TCPC_REG_STATUS1A_RXSOP		(1<<0)

#define TCPC_REG_INTERRUPTA	0x3E
#define TCPC_REG_INTERRUPTA_OCP_TEMP	(1<<7)
#define TCPC_REG_INTERRUPTA_TOGDONE	(1<<6)
#define TCPC_REG_INTERRUPTA_SOFTFAIL	(1<<5)
#define TCPC_REG_INTERRUPTA_RETRYFAIL	(1<<4)
#define TCPC_REG_INTERRUPTA_HARDSENT	(1<<3)
#define TCPC_REG_INTERRUPTA_TX_SUCCESS	(1<<2)
#define TCPC_REG_INTERRUPTA_SOFTRESET	(1<<1)
#define TCPC_REG_INTERRUPTA_HARDRESET	(1<<0)

#define TCPC_REG_INTERRUPTB	0x3F
#define TCPC_REG_INTERRUPTB_GCRCSENT		(1<<0)

#define TCPC_REG_STATUS0	0x40
#define TCPC_REG_STATUS0_VBUSOK		(1<<7)
#define TCPC_REG_STATUS0_ACTIVITY	(1<<6)
#define TCPC_REG_STATUS0_COMP		(1<<5)
#define TCPC_REG_STATUS0_CRC_CHK	(1<<4)
#define TCPC_REG_STATUS0_ALERT		(1<<3)
#define TCPC_REG_STATUS0_WAKE		(1<<2)
#define TCPC_REG_STATUS0_BC_LVL1	(1<<1) /* two-bit field */
#define TCPC_REG_STATUS0_BC_LVL0	(1<<0) /* two-bit field */

#define TCPC_REG_STATUS1	0x41
#define TCPC_REG_STATUS1_RXSOP2		(1<<7)
#define TCPC_REG_STATUS1_RXSOP1		(1<<6)
#define TCPC_REG_STATUS1_RX_EMPTY	(1<<5)
#define TCPC_REG_STATUS1_RX_FULL	(1<<4)
#define TCPC_REG_STATUS1_TX_EMPTY	(1<<3)
#define TCPC_REG_STATUS1_TX_FULL	(1<<2)

#define TCPC_REG_INTERRUPT	0x42
#define TCPC_REG_INTERRUPT_VBUSOK	(1<<7)
#define TCPC_REG_INTERRUPT_ACTIVITY	(1<<6)
#define TCPC_REG_INTERRUPT_COMP_CHNG	(1<<5)
#define TCPC_REG_INTERRUPT_CRC_CHK	(1<<4)
#define TCPC_REG_INTERRUPT_ALERT	(1<<3)
#define TCPC_REG_INTERRUPT_WAKE		(1<<2)
#define TCPC_REG_INTERRUPT_COLLISION	(1<<1)
#define TCPC_REG_INTERRUPT_BC_LVL	(1<<0)

#define TCPC_REG_FIFOS		0x43

/* Tokens defined for the FUSB302 TX FIFO */
enum fusb302_txfifo_tokens {
	FUSB302_TKN_TXON = 0xA1,
	FUSB302_TKN_SYNC1 = 0x12,
	FUSB302_TKN_SYNC2 = 0x13,
	FUSB302_TKN_SYNC3 = 0x1B,
	FUSB302_TKN_RST1 = 0x15,
	FUSB302_TKN_RST2 = 0x16,
	FUSB302_TKN_PACKSYM = 0x80,
	FUSB302_TKN_JAMCRC = 0xFF,
	FUSB302_TKN_EOP = 0x14,
	FUSB302_TKN_TXOFF = 0xFE,
};

/**
 * Initializes the FUSB302 to operate
 * as a SNK only.
 *
 * @param port  The i2c bus of the FUSB302B
 *
 * @returns EC_SUCCESS or EC_XXX on error
 */
int init_fusb302b(int port);

/**
 * Should be called from the interrupt generated
 * by the FUSB302. This function reads status
 * and interrupt registers in the FUSB302.
 */
int update_status_fusb302b(void);

/**
 * Returns true if VBUS is present, else false
 */
int is_vbus_present(void);

/*
 * Reads the status of the CC lines
 *
 * @returns EC_SUCCESS or EC_XXX on failure
 */
int get_cc(int *cc1, int *cc2);

#endif /* __CROS_EC_DRIVER_TCPM_FUSB302_H */
