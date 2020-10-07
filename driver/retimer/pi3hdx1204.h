/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PI3HDX1204 retimer.
 */

#ifndef __CROS_EC_USB_RETIMER_PI3HDX1204_H
#define __CROS_EC_USB_RETIMER_PI3HDX1204_H

#define PI3HDX1204_I2C_ADDR_FLAGS	0x60

/* Register Offset 0 - Activity */
#define PI3HDX1204_ACTIVITY_OFFSET	0

/* Register Offset 1 - Not Used */
#define PI3HDX1204_NOT_USED_OFFSET	1

/* Register Offset 2 - Enable */
#define PI3HDX1204_ENABLE_OFFSET	2
#define PI3HDX1204_ENABLE_ALL_CHANNELS	0xF0

/* Register Offset 3 - EQ setting BIT7-4:CH1, BIT3-0:CH0 */
#define PI3HDX1204_EQ_CH0_CH1_OFFSET	3

/* Register Offset 4 - EQ setting BIT7-4:CH3, BIT3-0:CH2 */
#define PI3HDX1204_EQ_CH2_CH3_OFFSET	4

/* EQ setting for two channel */
#define PI3HDX1204_EQ_DB25	0x00
#define PI3HDX1204_EQ_DB80	0x11
#define PI3HDX1204_EQ_DB110	0x22
#define PI3HDX1204_EQ_DB220	0x33
#define PI3HDX1204_EQ_DB410	0x44
#define PI3HDX1204_EQ_DB710	0x55
#define PI3HDX1204_EQ_DB900	0x66
#define PI3HDX1204_EQ_DB1030	0x77
#define PI3HDX1204_EQ_DB1180	0x88
#define PI3HDX1204_EQ_DB1390	0x99
#define PI3HDX1204_EQ_DB1530	0xAA
#define PI3HDX1204_EQ_DB1690	0xBB
#define PI3HDX1204_EQ_DB1790	0xCC
#define PI3HDX1204_EQ_DB1920	0xDD
#define PI3HDX1204_EQ_DB2050	0xEE
#define PI3HDX1204_EQ_DB2220	0xFF

/* Register Offset 5 - Output Voltage Swing Setting */
#define PI3HDX1204_VOD_OFFSET	5
#define PI3HDX1204_VOD_85_ALL_CHANNELS	0x00
#define PI3HDX1204_VOD_115_ALL_CHANNELS	0xFF

/* Register Offset 6 - Output De-emphasis Setting */
#define PI3HDX1204_DE_OFFSET	6
#define PI3HDX1204_DE_DB_0		0x00
#define PI3HDX1204_DE_DB_MINUS5		0x55
#define PI3HDX1204_DE_DB_MINUS7		0xAA
#define PI3HDX1204_DE_DB_MINUS10	0xFF

/* Delay for I2C to be ready after power on. */
#define PI3HDX1204_POWER_ON_DELAY_MS 2

/* Enable or disable the PI3HDX1204. */
int pi3hdx1204_enable(const int i2c_port,
		      const uint16_t i2c_addr_flags,
		      const int enable);

struct pi3hdx1204_tuning {
	uint8_t eq_ch0_ch1_offset;
	uint8_t eq_ch2_ch3_offset;
	uint8_t vod_offset;
	uint8_t de_offset;
};
extern const struct pi3hdx1204_tuning pi3hdx1204_tuning;

#endif /* __CROS_EC_USB_RETIMER_PI3HDX1204_H */
