/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SILERGY SY21612 buck-boost converter driver.
 */

#ifndef __CROS_EC_SY21612_H
#define __CROS_EC_SY21612_H

#include "gpio.h"

#ifndef SY21612_ADDR_FLAGS
#define SY21612_ADDR_FLAGS		0x71
#endif

enum sy21612_switching_freq {
	SY21612_FREQ_250KHZ = 0,
	SY21612_FREQ_500KHZ,
	SY21612_FREQ_750KHZ,
	SY21612_FREQ_1MHZ
};

enum sy21612_vbus_volt {
	SY21612_VBUS_5V = 2,
	SY21612_VBUS_7V,
	SY21612_VBUS_9V,
	SY21612_VBUS_12V,
	SY21612_VBUS_15V,
	SY21612_VBUS_20V,
};

enum sy21612_vbus_adj {
	SY21612_VBUS_M2_5 = 0,
	SY21612_VBUS_M1_25,
	SY21612_VBUS_0,
	SY21612_VBUS_1_25,
	SY21612_VBUS_2_5,
	SY21612_VBUS_3_75,
	SY21612_VBUS_5,
};

#define SY21612_CTRL1        0x00
#define SY21612_CTRL1_REG_EN         BIT(7)
#define SY21612_CTRL1_LOW_BAT_MASK   (7 << 4)
#define SY21612_CTRL1_LOW_BAT_10_2V  (0 << 4)
#define SY21612_CTRL1_LOW_BAT_10_7V  BIT(4)
#define SY21612_CTRL1_LOW_BAT_11_2V  (2 << 4)
#define SY21612_CTRL1_LOW_BAT_11_7V  (3 << 4)
#define SY21612_CTRL1_LOW_BAT_22_0V  (4 << 4)
#define SY21612_CTRL1_LOW_BAT_22_5V  (5 << 4)
#define SY21612_CTRL1_LOW_BAT_23_0V  (6 << 4)
#define SY21612_CTRL1_LOW_BAT_23_5V  (7 << 4)
#define SY21612_CTRL1_ADC_EN         BIT(3)
#define SY21612_CTRL1_ADC_AUTO_MODE  BIT(2)
#define SY21612_CTRL1_VBUS_NDISCHG   BIT(1)

#define SY21612_CTRL2        0x01
#define SY21612_CTRL2_FREQ_MASK      (3 << 6)
#define SY21612_CTRL2_FREQ_SHIFT     6
#define SY21612_CTRL2_FREQ_250K      (0 << 6)
#define SY21612_CTRL2_FREQ_500K      BIT(6)
#define SY21612_CTRL2_FREQ_750K      (2 << 6)
#define SY21612_CTRL2_FREQ_1M        (3 << 6)
#define SY21612_CTRL2_VBUS_MASK      (7 << 3)
#define SY21612_CTRL2_VBUS_SHIFT     3
#define SY21612_CTRL2_VBUS_5V        (2 << 3)
#define SY21612_CTRL2_VBUS_7V        (3 << 3)
#define SY21612_CTRL2_VBUS_9V        (4 << 3)
#define SY21612_CTRL2_VBUS_12V       (5 << 3)
#define SY21612_CTRL2_VBUS_15V       (6 << 3)
#define SY21612_CTRL2_VBUS_20V       (7 << 3)
#define SY21612_CTRL2_VBUS_ADJ_MASK  7
#define SY21612_CTRL2_VBUS_ADJ_SHIFT 0
#define SY21612_CTRL2_VBUS_ADJ_M2_5  0
#define SY21612_CTRL2_VBUS_ADJ_M1_25 1
#define SY21612_CTRL2_VBUS_ADJ_0     2
#define SY21612_CTRL2_VBUS_ADJ_1_25  3
#define SY21612_CTRL2_VBUS_ADJ_2_5   4
#define SY21612_CTRL2_VBUS_ADJ_3_75  5
#define SY21612_CTRL2_VBUS_ADJ_5     6

#define SY21612_PROT1        0x02
#define SY21612_PROT1_I_THRESH_MASK   (7 << 5)
#define SY21612_PROT1_I_THRESH_18MV   (0 << 5)
#define SY21612_PROT1_I_THRESH_22MV   BIT(5)
#define SY21612_PROT1_I_THRESH_27MV   (2 << 5)
#define SY21612_PROT1_I_THRESH_31MV   (3 << 5)
#define SY21612_PROT1_I_THRESH_36MV   (4 << 5)
#define SY21612_PROT1_I_THRESH_45MV   (5 << 5)
#define SY21612_PROT1_I_THRESH_54MV   (6 << 5)
#define SY21612_PROT1_I_THRESH_64MV   (7 << 5)
#define SY21612_PROT1_OVP_THRESH_MASK (3 << 3)
#define SY21612_PROT1_OVP_THRESH_110  (0 << 3)
#define SY21612_PROT1_OVP_THRESH_115  BIT(3)
#define SY21612_PROT1_OVP_THRESH_120  (2 << 3)
#define SY21612_PROT1_OVP_THRESH_125  (3 << 3)
#define SY21612_PROT1_UVP_THRESH_MASK (3 << 1)
#define SY21612_PROT1_UVP_THRESH_50   (0 << 1)
#define SY21612_PROT1_UVP_THRESH_60   BIT(1)
#define SY21612_PROT1_UVP_THRESH_70   (2 << 1)
#define SY21612_PROT1_UVP_THRESH_80   (3 << 1)

#define SY21612_PROT2        0x03
#define SY21612_PROT2_I_LIMIT_MASK    (3 << 6)
#define SY21612_PROT2_I_LIMIT_6A      (0 << 6)
#define SY21612_PROT2_I_LIMIT_8A      (2 << 6)
#define SY21612_PROT2_I_LIMIT_10A     (3 << 6)
#define SY21612_PROT2_OCP_AUTORECOVER BIT(5)
#define SY21612_PROT2_UVP_AUTORECOVER BIT(4)
#define SY21612_PROT2_OTP_AUTORECOVER BIT(3)
#define SY21612_PROT2_SINK_MODE       BIT(2)

#define SY21612_STATE        0x04
#define SY21612_STATE_POWER_GOOD      BIT(7)
#define SY21612_STATE_VBAT_LT_VBUS    BIT(6)
#define SY21612_STATE_VBAT_LOW        BIT(5)

#define SY21612_INT          0x05
#define SY21612_INT_ADC_READY         BIT(7)
#define SY21612_INT_VBUS_OCP          BIT(6)
#define SY21612_INT_INDUCTOR_OCP      BIT(5)
#define SY21612_INT_UVP               BIT(4)
#define SY21612_INT_OTP               BIT(3)

/* Battery voltage range: 0 ~ 25V */
#define SY21612_VBAT_VOLT    0x06

/* VBUS voltage range: 0 ~ 25V */
#define SY21612_VBUS_VOLT    0x07

/* Output current sense voltage range 0 ~ 67mV */
#define SY21612_VBUS_CURRENT 0x08

/* Enable or disable the regulator */
int sy21612_enable_regulator(int enable);
/* Enable internal adc */
int sy21612_enable_adc(int enable);
/* Set ADC mode to single or auto */
int sy21612_set_adc_mode(int auto_mode);
/* Enable VBUS auto discharge when regulator is disabled */
int sy21612_set_vbus_discharge(int auto_discharge);
/* Set buck-boost switching frequency */
int sy21612_set_switching_freq(enum sy21612_switching_freq freq);
/* Set VBUS output voltage */
int sy21612_set_vbus_volt(enum sy21612_vbus_volt volt);
/* Adjust VBUS output voltage */
int sy21612_set_vbus_adj(enum sy21612_vbus_adj adj);
/* Set bidirection mode */
int sy21612_set_sink_mode(int sink_mode);
/* Get power good status */
int sy21612_is_power_good(void);
/* Read and clear interrupt flags */
int sy21612_read_clear_int(void);
/* Get VBUS voltage in mV */
int sy21612_get_vbat_voltage(void);
/* Get VBUS voltage in mV */
int sy21612_get_vbus_voltage(void);
/* Get VBUS current in mA */
int sy21612_get_vbus_current(void);
/* Interrupt handler */
void sy21612_int(enum gpio_signal signal);

#endif /* __CROS_EC_SY21612_H */
