/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25890/bq25892/bq25895 battery charger driver.
 */

#ifndef __CROS_EC_BQ2589X_H
#define __CROS_EC_BQ2589X_H

/* Registers */
#define BQ2589X_REG_INPUT_CURR      0x00
#define BQ2589X_REG_VINDPM          0x01
#define BQ2589X_REG_CFG1            0x02
#define BQ2589X_REG_CFG2            0x03
#define BQ2589X_REG_CHG_CURR        0x04
#define BQ2589X_REG_PRE_CHG_CURR    0x05
#define BQ2589X_REG_CHG_VOLT        0x06
#define BQ2589X_REG_TIMER           0x07
#define BQ2589X_REG_IR_COMP         0x08
#define BQ2589X_REG_FORCE           0x09
#define BQ2589X_REG_BOOST_MODE      0x0A
#define BQ2589X_REG_STATUS          0x0B /* Read-only */
#define BQ2589X_REG_FAULT           0x0C /* Read-only */
#define BQ2589X_REG_VINDPM_THRESH   0x0D
#define BQ2589X_REG_ADC_BATT_VOLT   0x0E /* Read-only */
#define BQ2589X_REG_ADC_SYS_VOLT    0x0F /* Read-only */
#define BQ2589X_REG_ADC_TS          0x10 /* Read-only */
#define BQ2589X_REG_ADC_VBUS_VOLT   0x11 /* Read-only */
#define BQ2589X_REG_ADC_CHG_CURR    0x12 /* Read-only */
#define BQ2589X_REG_ADC_INPUT_CURR  0x13 /* Read-only */
#define BQ2589X_REG_ID              0x14

/* REG00 : input current register bit definitions */
#define BQ2589X_INPUT_CURR_EN_HIZ  (1<<7)
#define BQ2589X_INPUT_CURR_EN_ILIM (1<<6)

/* REG02 : first configuration register bit definitions */
#define BQ2589X_CFG1_CONV_START    (1<<7)
#define BQ2589X_CFG1_ICO_EN        (1<<4)
#define BQ2589X_CFG1_AUTO_DPDM_EN  (1<<0)

/* REG03 : second configuration register bit definitions */
#define BQ2589X_CFG2_CHG_CONFIG    (1<<4)
#define BQ2589X_CFG2_OTG_CONFIG    (1<<5)
#define BQ2589X_CFG2_WD_RST        (1<<6)

/* REG08 : IR compensation definitions */
#define BQ2589X_IR_BAT_COMP_140MOHM (7 << 5)
#define BQ2589X_IR_BAT_COMP_120MOHM (6 << 5)
#define BQ2589X_IR_BAT_COMP_100MOHM (5 << 5)
#define BQ2589X_IR_BAT_COMP_80MOHM  (4 << 5)
#define BQ2589X_IR_BAT_COMP_60MOHM  (3 << 5)
#define BQ2589X_IR_BAT_COMP_40MOHM  (2 << 5)
#define BQ2589X_IR_BAT_COMP_20MOHM  BIT(5)
#define BQ2589X_IR_BAT_COMP_0MOHM   (0 << 5)
#define BQ2589X_IR_VCLAMP_224MV     (7 << 2)
#define BQ2589X_IR_VCLAMP_192MV     (6 << 2)
#define BQ2589X_IR_VCLAMP_160MV     (5 << 2)
#define BQ2589X_IR_VCLAMP_128MV     (4 << 2)
#define BQ2589X_IR_VCLAMP_96MV      (3 << 2)
#define BQ2589X_IR_VCLAMP_64MV      (2 << 2)
#define BQ2589X_IR_VCLAMP_32MV      BIT(2)
#define BQ2589X_IR_VCLAMP_0MV       (0 << 2)
#define BQ2589X_IR_TREG_120C        (3 << 0)
#define BQ2589X_IR_TREG_100C        (2 << 0)
#define BQ2589X_IR_TREG_80C         BIT(0)
#define BQ2589X_IR_TREG_60C         (0 << 0)

#define BQ2589X_IR_COMP_DEFAULT (BQ2589X_IR_TREG_120C | BQ2589X_IR_VCLAMP_0MV |\
				 BQ2589X_IR_BAT_COMP_0MOHM)

#define BQ2589X_TERM_CURRENT_LIMIT_DEFAULT 256

/* 5V VBUS Boost settings */
#define BQ2589X_BOOSTV_MV(mv)       (((((mv) - 4550)/64) & 0xF) << 4)
#define BQ2589X_BOOSTV_DEFAULT      BQ2589X_BOOSTV_MV(4998)
#define BQ2589X_BOOST_LIM_500MA     0x00
#define BQ2589X_BOOST_LIM_750MA     0x01
#define BQ2589X_BOOST_LIM_1200MA    0x02
#define BQ2589X_BOOST_LIM_1400MA    0x03
#define BQ2589X_BOOST_LIM_1650MA    0x04
#define BQ2589X_BOOST_LIM_1875MA    0x05
#define BQ2589X_BOOST_LIM_2150MA    0x06
#define BQ2589X_BOOST_LIM_2450MA    0x07
#define BQ2589X_BOOST_LIM_DEFAULT   BQ2589X_BOOST_LIM_1400MA
#define BQ2589X_BOOST_DEFAULT       (BQ2589X_BOOST_LIM_DEFAULT |\
				     BQ2589X_BOOSTV_DEFAULT)

/* REG14: Device ID, reset and ICO status */
#define BQ2589X_DEVICE_ID_MASK      0x38
#define BQ25890_DEVICE_ID           0x18
#define BQ25892_DEVICE_ID           0x00
#define BQ25895_DEVICE_ID           0x38

#define BQ2589X_ID_ICO_OPTIMIZED    0x40


/* Variant-specific configuration */
#if   defined(CONFIG_CHARGER_BQ25890)
#define BQ2589X_DEVICE_ID    BQ25890_DEVICE_ID
#define BQ2589X_ADDR_FLAGS          0x6A
#elif defined(CONFIG_CHARGER_BQ25895)
#define BQ2589X_DEVICE_ID    BQ25895_DEVICE_ID
#define BQ2589X_ADDR_FLAGS          0x6A
#elif defined(CONFIG_CHARGER_BQ25892)
#define BQ2589X_DEVICE_ID    BQ25892_DEVICE_ID
#define BQ2589X_ADDR_FLAGS          0x6B
#else
#error BQ2589X unknown variant
#endif

#endif /* __CROS_EC_BQ2589X_H */
