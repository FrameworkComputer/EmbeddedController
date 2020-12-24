/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Silicon Image SI1141/SI1142 light sensor driver
 */

#ifndef __CROS_EC_ALS_SI114X_H
#define __CROS_EC_ALS_SI114X_H

#define SI114X_ADDR_FLAGS               0x5a

#define SI114X_PART_ID		0x00
#define SI114X_SEQ_ID		0x02

#define SI114X_INT_CFG		0x03
#define SI114X_INT_CFG_INT_OE		BIT(0)

#define SI114X_IRQ_ENABLE	0x04
#define SI114X_IRQ_ENABLE_PS3_IE	BIT(4)
#define SI114X_IRQ_ENABLE_PS2_IE	BIT(3)
#define SI114X_IRQ_ENABLE_PS1_IE	BIT(2)
#define SI114X_IRQ_ENABLE_ALS_IE_INT1	BIT(1)
#define SI114X_IRQ_ENABLE_ALS_IE_INT0	BIT(0)

#define SI114X_HW_KEY		0x07
#define SI114X_HW_KEY_VALUE		0x17

#define SI114X_MEAS_RATE	0x08
#define SI114X_ALS_RATE		0x09
#define SI114X_PS_RATE		0x0A

#define SI114X_PS_LED21		0x0F
#define SI114X_PS_LED3		0x10
#define SI114X_NUM_LEDS  (CONFIG_ALS_SI114X - 0x40)

#define SI114X_PARAM_WR		0x17
#define SI114X_COMMAND		0x18

#define SI114X_COMMAND_PARAM_QUERY	0x80
#define SI114X_COMMAND_PARAM_SET	0xA0
#define SI114X_PARAM_CHLIST			0x01
#define SI114X_PARAM_CHLIST_EN_ALS_VIS			BIT(4)
#define SI114X_PARAM_CHLIST_EN_PS3			BIT(2)
#define SI114X_PARAM_CHLIST_EN_PS2			BIT(1)
#define SI114X_PARAM_CHLIST_EN_PS1			BIT(0)
#define SI114X_PARAM_PS_ADC_COUNTER		0x0A
#define SI114X_PARAM_PS_ADC_GAIN		0x0B
#define SI114X_PARAM_PS_ADC_MISC		0x0C
#define SI114X_PARAM_PS_ADC_MISC_MODE			BIT(2)
#define SI114X_PARAM_PS_ADC_MISC_MODE_NORMAL_PROXIMITY	BIT(2)
#define SI114X_PARAM_ALS_VIS_ADC_COUNTER	0x10
#define SI114X_PARAM_ALS_VIS_ADC_GAIN		0x11
#define SI114X_PARAM_ALS_VIS_ADC_MISC		0x12

#define SI114X_COMMAND_RESET		0x01
#define SI114X_COMMAND_PS_FORCE		0x05
#define SI114X_COMMAND_ALS_FORCE	0x06

#define SI114X_IRQ_STATUS	0x21
#define SI114X_ALS_VIS_DATA0	0x22

#define SI114X_PARAM_RD		0x2E

/* Proximity sensor finds an object within 5 cm, disable light sensor */
#define SI114X_COVERED_THRESHOLD        5
#define SI114X_OVERFLOW                 0xffff

/* Time to wait before re-initializing the device if access is denied */
#define SI114X_DENIED_THRESHOLD		(10 * SECOND)

/* Delay used for deferred callback when polling is enabled */
#define SI114x_POLLING_DELAY (8 * MSEC)

/* Min and Max sampling frequency in mHz */
#define SI114X_PROX_MIN_FREQ            504
#define SI114X_PROX_MAX_FREQ            50000
#define SI114X_LIGHT_MIN_FREQ           504
#define SI114X_LIGHT_MAX_FREQ           50000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= SI114X_PROX_MAX_FREQ)
#error "EC too slow for light sensor"
#endif

extern const struct accelgyro_drv si114x_drv;

enum si114x_state {
	SI114X_NOT_READY,
	SI114X_IDLE,
	SI114X_ALS_IN_PROGRESS,
	SI114X_ALS_IN_PROGRESS_PS_PENDING,
	SI114X_PS_IN_PROGRESS,
	SI114X_PS_IN_PROGRESS_ALS_PENDING,
};

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
	uint8_t covered;
	struct si114x_typed_data_t type_data[2];
};

#define SI114X_GET_DATA(_s) \
	((struct si114x_drv_data_t *)(_s)->drv_data)

#define SI114X_GET_TYPED_DATA(_s) \
	(&SI114X_GET_DATA(_s)->type_data[(_s)->type - MOTIONSENSE_TYPE_PROX])

void si114x_interrupt(enum gpio_signal signal);

#endif	/* __CROS_EC_ALS_SI114X_H */
