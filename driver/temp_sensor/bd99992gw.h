/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_BD99992GW_H
#define __CROS_EC_TEMP_SENSOR_BD99992GW_H

#define BD99992GW_I2C_ADDR		0x60

/* ADC channels */
enum bd99992gw_adc_channel {
	BD99992GW_ADC_CHANNEL_NONE      = -1,
	BD99992GW_ADC_CHANNEL_BATTERY   = 0,
	BD99992GW_ADC_CHANNEL_AC        = 1,
	BD99992GW_ADC_CHANNEL_SYSTHERM0 = 2,
	BD99992GW_ADC_CHANNEL_SYSTHERM1 = 3,
	BD99992GW_ADC_CHANNEL_SYSTHERM2 = 4,
	BD99992GW_ADC_CHANNEL_SYSTHERM3 = 5,
	BD99992GW_ADC_CHANNEL_DIE_TEMP  = 6,
	BD99992GW_ADC_CHANNEL_VDC       = 7,
	BD99992GW_ADC_CHANNEL_COUNT     = 8,
};

/* Registers */
#define BD99992GW_REG_IRQLVL1		0x02
#define BD99992GW_IRQLVL1_ADC		 (1 << 1) /* ADC IRQ asserted */

#define BD99992GW_REG_ADC1INT		0x03
#define BD99992GW_ADC1INT_RND		 (1 << 0) /* RR cycle completed */

#define BD99992GW_REG_MADC1INT		0x0a
#define BD99992GW_MADC1INT_RND		 (1 << 0) /* RR cycle mask */

#define BD99992GW_REG_IRQLVL1MSK	0x13
#define BD99992GW_IRQLVL1MSK_MADC	 (1 << 1) /* ADC IRQ mask */

#define BD99992GW_REG_ADC1CNTL1		0x80
#define BD99992GW_ADC1CNTL1_SLP27MS	 (0x6 << 3) /* 27ms between pass */
#define BD99992GW_ADC1CNTL1_NOLOOP	 (0x7 << 3) /* Single loop pass only */
#define BD99992GW_ADC1CNTL1_ADPAUSE	 (1 << 2)   /* ADC pause */
#define BD99992GW_ADC1CNTL1_ADSTRT	 (1 << 1)   /* ADC start */
#define BD99992GW_ADC1CNTL1_ADEN	 (1 << 0)   /* ADC enable */

#define BD99992GW_REG_ADC1CNTL2		0x81
#define BD99992GW_ADC1CNTL2_ADCTHERM	 (1 << 0) /* Enable ADC sequencing */

 /* ADC1 Pointer file regs - assign to proper bd99992gw_adc_channel */
#define BD99992GW_ADC_POINTER_REG_COUNT	8
#define BD99992GW_REG_ADC1ADDR0		0x82
#define BD99992GW_REG_ADC1ADDR1		0x83
#define BD99992GW_REG_ADC1ADDR2		0x84
#define BD99992GW_REG_ADC1ADDR3		0x85
#define BD99992GW_REG_ADC1ADDR4		0x86
#define BD99992GW_REG_ADC1ADDR5		0x87
#define BD99992GW_REG_ADC1ADDR6		0x88
#define BD99992GW_REG_ADC1ADDR7		0x89
#define BD99992GW_ADC1ADDR_STOP		 (1 << 3) /* Last conversion channel */

/* Result registers */
#define BD99992GW_REG_ADC1DATA0L	0x95
#define BD99992GW_REG_ADC1DATA0H	0x96
#define BD99992GW_REG_ADC1DATA1L	0x97
#define BD99992GW_REG_ADC1DATA1H	0x98
#define BD99992GW_REG_ADC1DATA2L	0x99
#define BD99992GW_REG_ADC1DATA2H	0x9a
#define BD99992GW_REG_ADC1DATA3L	0x9b
#define BD99992GW_REG_ADC1DATA3H	0x9c
#define BD99992GW_REG_ADC1DATA4L	0x9d
#define BD99992GW_REG_ADC1DATA4H	0x9e
#define BD99992GW_REG_ADC1DATA5L	0x9f
#define BD99992GW_REG_ADC1DATA5H	0xa0
#define BD99992GW_REG_ADC1DATA6L	0xa1
#define BD99992GW_REG_ADC1DATA6H	0xa2
#define BD99992GW_REG_ADC1DATA7L	0xa3
#define BD99992GW_REG_ADC1DATA7H	0xa4

/**
 * Get the latest value from the sensor.
 *
 * @param idx		ADC channel to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int bd99992gw_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_TEMP_SENSOR_BD99992GW_H */
