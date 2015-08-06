/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Image SI1141/SI1142 light sensor driver
 */

/*
 * si114x.c - Support for Silabs si114x combined ambient light and
 * proximity sensor
 *
 * Copyright 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for si114x (7-bit I2C slave address 0x5a) with sequencer
 * version >= A03
 */

#ifndef __CROS_EC_ALS_SI114X_H
#define __CROS_EC_ALS_SI114X_H

#define SI114X_ADDR                     (0x5a << 1)

#define SI114X_REG_PART_ID		0x00
#define SI114X_SI1141_ID                     0x41
#define SI114X_SI1142_ID                     0x42
#define SI114X_SI1143_ID                     0x43

#define SI114X_NUM_LEDS  (CONFIG_ALS_SI114X - 0x40)

#define SI114X_REG_REV_ID		0x01
#define SI114X_REG_SEQ_ID		0x02
#define SI114X_REG_INT_CFG		0x03
#define SI114X_REG_IRQ_ENABLE		0x04
#define SI114X_REG_IRQ_MODE1		0x05
#define SI114X_REG_IRQ_MODE2		0x06
#define SI114X_REG_HW_KEY		0x07
/* RATE stores a 16 bit value compressed to 8 bit */
/* Not used, the sensor is in force mode */
#define SI114X_REG_MEAS_RATE		0x08


#define SI114X_REG_ALS_RATE		0x09
#define SI114X_REG_PS_RATE		0x0a
#define SI114X_REG_ALS_LOW_TH0		0x0b
#define SI114X_REG_ALS_LOW_TH1		0x0c
#define SI114X_REG_ALS_HI_TH0		0x0d
#define SI114X_REG_ALS_HI_TH1		0x0e
#define SI114X_REG_PS_LED21		0x0f
#define SI114X_REG_PS_LED3		0x10
/*
 * for rev A10 and below TH0 stores a 16 bit value compressed to 8 bit and
 * TH1 is not used; newer revision have the LSB in TH0 and the MSB in TH1
 */
#define SI114X_REG_PS1_TH0		0x11
#define SI114X_REG_PS1_TH1		0x12
#define SI114X_REG_PS2_TH0		0x13
#define SI114X_REG_PS2_TH1		0x11
#define SI114X_REG_PS3_TH0		0x15
#define SI114X_REG_PS3_TH1		0x16
#define SI114X_REG_PARAM_WR		0x17
#define SI114X_REG_COMMAND		0x18
#define SI114X_REG_RESPONSE		0x20
#define SI114X_REG_IRQ_STATUS		0x21
#define SI114X_REG_ALSVIS_DATA0		0x22
#define SI114X_REG_ALSVIS_DATA1		0x23
#define SI114X_REG_ALSIR_DATA0		0x24
#define SI114X_REG_ALSIR_DATA1		0x25
#define SI114X_REG_PS1_DATA0		0x26
#define SI114X_REG_PS1_DATA1		0x27
#define SI114X_REG_PS2_DATA0		0x28
#define SI114X_REG_PS2_DATA1		0x29
#define SI114X_REG_PS3_DATA0		0x2a
#define SI114X_REG_PS3_DATA1		0x2b
#define SI114X_REG_AUX_DATA0		0x2c
#define SI114X_REG_AUX_DATA1		0x2d
#define SI114X_REG_PARAM_RD		0x2e
#define SI114X_REG_CHIP_STAT		0x30

/* helper to figure out PS_LED register / shift per channel */
#define SI114X_PS_LED_REG(ch) \
	(((ch) == 2) ? SI114X_REG_PS_LED3 : SI114X_REG_PS_LED21)
#define SI114X_PS_LED_SHIFT(ch) \
	(((ch) == 1) ? 4 : 0)

/* Parameter offsets */
#define SI114X_PARAM_I2C_ADDR		0x00
#define SI114X_PARAM_CHLIST		0x01
#define SI114X_PARAM_PSLED12_SELECT	0x02
#define SI114X_PARAM_PSLED3_SELECT	0x03
#define SI114X_PARAM_FILTER_EN		0x04
#define SI114X_PARAM_PS_ENCODING	0x05
#define SI114X_PARAM_ALS_ENCODING	0x06
#define SI114X_PARAM_PS1_ADC_MUX	0x07
#define SI114X_PARAM_PS2_ADC_MUX	0x08
#define SI114X_PARAM_PS3_ADC_MUX	0x09
#define SI114X_PARAM_PS_ADC_COUNTER	0x0a
#define SI114X_PARAM_PS_ADC_GAIN	0x0b
#define SI114X_PARAM_PS_ADC_MISC	0x0c
#define SI114X_PARAM_PS_ADC_MISC_HIGH_RANGE       0x20
#define SI114X_PARAM_PS_ADC_MISC_NORMAL_MODE      0x04
#define SI114X_PARAM_ALS_ADC_MUX	0x0d
#define SI114X_PARAM_ALSIR_ADC_MUX	0x0e
#define SI114X_PARAM_AUX_ADC_MUX	0x0f
#define SI114X_PARAM_ALSVIS_ADC_COUNTER	0x10
#define SI114X_PARAM_ALSVIS_ADC_GAIN	0x11
#define SI114X_PARAM_ALSVIS_ADC_MISC	0x12
#define SI114X_PARAM_ALS_HYST		0x16
#define SI114X_PARAM_PS_HYST		0x17
#define SI114X_PARAM_PS_HISTORY		0x18
#define SI114X_PARAM_ALS_HISTORY	0x19
#define SI114X_PARAM_ADC_OFFSET		0x1a
#define SI114X_PARAM_SLEEP_CTRL		0x1b
#define SI114X_PARAM_LED_RECOVERY	0x1c
#define SI114X_PARAM_ALSIR_ADC_COUNTER	0x1d
#define SI114X_PARAM_ALSIR_ADC_GAIN	0x1e
#define SI114X_PARAM_ALSIR_ADC_MISC	0x1f

/* Channel enable masks for CHLIST parameter */
#define SI114X_CHLIST_EN_PS1		0x01
#define SI114X_CHLIST_EN_PS2		0x02
#define SI114X_CHLIST_EN_PS3		0x04
#define SI114X_CHLIST_EN_ALSVIS		0x10
#define SI114X_CHLIST_EN_ALSIR		0x20
#define SI114X_CHLIST_EN_AUX		0x40

/* Signal range mask for ADC_MISC parameter */
#define SI114X_MISC_RANGE		0x20

/* Commands for REG_COMMAND */
#define SI114X_CMD_NOP			0x00
#define SI114X_CMD_RESET		0x01
#define SI114X_CMD_BUSADDR		0x02
#define SI114X_CMD_PS_FORCE		0x05
#define SI114X_CMD_ALS_FORCE		0x06
#define SI114X_CMD_PSALS_FORCE		0x07
#define SI114X_CMD_PS_PAUSE		0x09
#define SI114X_CMD_ALS_PAUSE		0x0a
#define SI114X_CMD_PSALS_PAUSE		0x0b
#define SI114X_CMD_PS_AUTO		0x0d
#define SI114X_CMD_ALS_AUTO		0x0e
#define SI114X_CMD_PSALS_AUTO		0x0f
#define SI114X_CMD_PARAM_QUERY		0x80
#define SI114X_CMD_PARAM_SET		0xa0
#define SI114X_CMD_PARAM_AND		0xc0
#define SI114X_CMD_PARAM_OR		0xe0

/* Interrupt configuration masks for INT_CFG register */
#define SI114X_INT_CFG_OE		0x01 /* enable interrupt */
#define SI114X_INT_CFG_MODE		0x02 /* auto reset interrupt pin */

/* Interrupt enable masks for IRQ_ENABLE register */
#define SI114X_CMD_IE			0x20
#define SI114X_PS3_IE			0x10
#define SI114X_PS2_IE			0x08
#define SI114X_PS1_IE			0x04
#define SI114X_ALS_INT1_IE		0x02
#define SI114X_ALS_INT0_IE		0x01
#define SI114X_ALS_INT_FLAG \
	(SI114X_ALS_INT1_IE | SI114X_ALS_INT0_IE)
#define SI114X_PS_INT_FLAG \
	(SI114X_PS3_IE | SI114X_PS2_IE | SI114X_PS1_IE)


/* Interrupt mode masks for IRQ_MODE1 register */
#define SI114X_PS2_IM_GREATER		0xc0
#define SI114X_PS2_IM_CROSS		0x40
#define SI114X_PS1_IM_GREATER		0x30
#define SI114X_PS1_IM_CROSS		0x10

/* Interrupt mode masks for IRQ_MODE2 register */
#define SI114X_CMD_IM_ERROR		0x04
#define SI114X_PS3_IM_GREATER		0x03
#define SI114X_PS3_IM_CROSS		0x01

/* Measurement rate settings */
#define SI114X_MEAS_RATE_FORCED		0x00
#define SI114X_MEAS_RATE_10MS		0x84
#define SI114X_MEAS_RATE_20MS		0x94
#define SI114X_MEAS_RATE_100MS		0xb9
#define SI114X_MEAS_RATE_496MS		0xdf
#define SI114X_MEAS_RATE_1984MS		0xff

/* ALS rate settings relative to measurement rate */
#define SI114X_ALS_RATE_OFF		0x00
#define SI114X_ALS_RATE_1X		0x08
#define SI114X_ALS_RATE_10X		0x32
#define SI114X_ALS_RATE_100X		0x69

/* PS rate settings relative to measurement rate */
#define SI114X_PS_RATE_OFF		0x00
#define SI114X_PS_RATE_1X		0x08
#define SI114X_PS_RATE_10X		0x32
#define SI114X_PS_RATE_100X		0x69

/* Sequencer revision from SEQ_ID */
#define SI114X_SEQ_REV_A01		0x01
#define SI114X_SEQ_REV_A02		0x02
#define SI114X_SEQ_REV_A03		0x03
#define SI114X_SEQ_REV_A10		0x08
#define SI114X_SEQ_REV_A11		0x09

extern const struct accelgyro_drv si114x_drv;

enum si114x_state {
	SI114X_IDLE,
	SI114X_ALS_IN_PROGRESS,
	SI114X_ALS_IN_PROGRESS_PS_PENDING,
	SI114X_PS_IN_PROGRESS,
	SI114X_PS_IN_PROGRESS_ALS_PENDING,
};

/**
 * struct si114x_data - si114x chip state data
 * @client:	I2C client
 **/
struct si114x_typed_data_t {
	uint8_t base_data_reg;
	uint8_t irq_flags;
	/* requested frequency, in mHz */
	int rate;
	/* the coef is scale.uscale */
	int16_t scale;
	uint16_t uscale;
	int16_t offset;
};

struct si114x_drv_data_t {
	enum si114x_state state;
	struct si114x_typed_data_t type_data[2];
};

#define SI114X_GET_DATA(_s) \
	((struct si114x_drv_data_t *)(_s)->drv_data)

#define SI114X_GET_TYPED_DATA(_s) \
	(&SI114X_GET_DATA(_s)->type_data[(_s)->type - MOTIONSENSE_TYPE_PROX])

extern struct si114x_drv_data_t g_si114x_data;
void si114x_interrupt(enum gpio_signal signal);

#endif	/* __CROS_EC_ALS_SI114X_H */
