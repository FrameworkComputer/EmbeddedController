/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP ITE EC specific configuration */

#ifndef __CROS_EC_ITE_EC_H
#define __CROS_EC_ITE_EC_H

/* USB PD config */
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 2

/* Optional feature - used by ITE */
#define CONFIG_IT83XX_FLASH_CLOCK_48MHZ

/* ADC channels */
#define ADC_TEMP_SNS_AMBIENT_CHANNEL	CHIP_ADC_CH13
#define ADC_TEMP_SNS_DDR_CHANNEL	CHIP_ADC_CH15
#define ADC_TEMP_SNS_SKIN_CHANNEL	CHIP_ADC_CH6
#define ADC_TEMP_SNS_VR_CHANNEL		CHIP_ADC_CH1

#ifdef CONFIG_USBC_VCONN
	#define CONFIG_USBC_VCONN_SWAP
	/* delay to turn on/off vconn */
	#define PD_VCONN_SWAP_DELAY 5000 /* us */
#endif
#endif /* __CROS_EC_ITE_EC_H */
