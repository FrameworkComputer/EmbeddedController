/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI INA219/231 Current/Power monitor driver.
 */

#ifndef INA2XX_H
#define INA2XX_H

#define INA2XX_REG_CONFIG     0x00
#define INA2XX_REG_SHUNT_VOLT 0x01
#define INA2XX_REG_BUS_VOLT   0x02
#define INA2XX_REG_POWER      0x03
#define INA2XX_REG_CURRENT    0x04
#define INA2XX_REG_CALIB      0x05
#define INA2XX_REG_MASK       0x06
#define INA2XX_REG_ALERT      0x07

#define INA2XX_CONFIG_MODE_MASK     (7 << 0)
#define INA2XX_CONFIG_MODE_PWRDWN   (0 << 0)
#define INA2XX_CONFIG_MODE_SHUNT    (1 << 0)
#define INA2XX_CONFIG_MODE_BUS      (1 << 1)
#define INA2XX_CONFIG_MODE_TRG      (0 << 2)
#define INA2XX_CONFIG_MODE_CONT     (1 << 2)

/* Conversion time for bus and shunt in micro-seconds */
enum ina2xx_conv_time {
	INA2XX_CONV_TIME_140  = 0x00,
	INA2XX_CONV_TIME_204  = 0x01,
	INA2XX_CONV_TIME_332  = 0x02,
	INA2XX_CONV_TIME_588  = 0x03,
	INA2XX_CONV_TIME_1100 = 0x04,
	INA2XX_CONV_TIME_2116 = 0x05,
	INA2XX_CONV_TIME_4156 = 0x06,
	INA2XX_CONV_TIME_8244 = 0x07,
};
#define INA2XX_CONV_TIME_MASK 0x7
#define INA2XX_CONFIG_SHUNT_CONV_TIME(t) ((t) << 3)
#define INA2XX_CONFIG_BUS_CONV_TIME(t)   ((t) << 6)

#define INA2XX_CONFIG_AVG_1        (0 << 9)
#define INA2XX_CONFIG_AVG_4        (1 << 9)
#define INA2XX_CONFIG_AVG_16       (2 << 9)
#define INA2XX_CONFIG_AVG_64       (3 << 9)
#define INA2XX_CONFIG_AVG_128      (4 << 9)
#define INA2XX_CONFIG_AVG_256      (5 << 9)
#define INA2XX_CONFIG_AVG_512      (6 << 9)
#define INA2XX_CONFIG_AVG_1024     (7 << 9)

#define INA2XX_MASK_EN_LEN         (1 << 0)
#define INA2XX_MASK_EN_APOL        (1 << 1)
#define INA2XX_MASK_EN_OVF         (1 << 2)
#define INA2XX_MASK_EN_CVRF        (1 << 3)
#define INA2XX_MASK_EN_AFF         (1 << 4)
#define INA2XX_MASK_EN_CNVR        (1 << 10)
#define INA2XX_MASK_EN_POL         (1 << 11)
#define INA2XX_MASK_EN_BUL         (1 << 12)
#define INA2XX_MASK_EN_BOL         (1 << 13)
#define INA2XX_MASK_EN_SUL         (1 << 14)
#define INA2XX_MASK_EN_SOL        (1 << 15)


#if defined(CONFIG_INA231) && defined(CONFIG_INA219)
#error CONFIG_INA231 and CONFIG_INA219 must not be both defined.
#endif

#ifdef CONFIG_INA231

/* Calibration value to get current LSB = 1mA */
#define INA2XX_CALIB_1MA(rsense_mohm) (5120/(rsense_mohm))
/* Bus voltage: mV per LSB */
#define INA2XX_BUS_MV(reg) ((reg) * 125 / 100)
/* Shunt voltage: uV per LSB */
#define INA2XX_SHUNT_UV(reg) ((reg) * 25 / 10)
/* Power LSB: mW per current LSB */
#define INA2XX_POW_MW(reg) ((reg) * 25 * 1/*Current mA/LSB*/)

#else /* CONFIG_INA219 */

/* Calibration value to get current LSB = 1mA */
#define INA2XX_CALIB_1MA(rsense_mohm) (40960/(rsense_mohm))
/* Bus voltage: mV per LSB */
#define INA2XX_BUS_MV(reg) ((reg) / 2)
/* Shunt voltage: uV per LSB */
#define INA2XX_SHUNT_UV(reg) ((reg) * 2)
/* Power LSB: mW per current LSB */
#define INA2XX_POW_MW(reg) ((reg) * 20 * 1/*Current mA/LSB*/)

#endif


/* Read INA2XX register. */
uint16_t ina2xx_read(uint8_t idx, uint8_t reg);

/* Write INA2XX register. */
int ina2xx_write(uint8_t idx, uint8_t reg, uint16_t val);

/* Set measurement parameters */
int ina2xx_init(uint8_t idx, uint16_t config, uint16_t calib);

/* Return bus voltage in milliVolts */
int ina2xx_get_voltage(uint8_t idx);

/* Return current in milliAmps */
int ina2xx_get_current(uint8_t idx);

/* Return power in milliWatts */
int ina2xx_get_power(uint8_t idx);

#endif /* INA2XX_H */
