/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP ITE EC specific configuration */

#ifndef __CROS_EC_ITE_EC_H
#define __CROS_EC_ITE_EC_H

/* Optional feature - used by ITE */
#define CONFIG_IT83XX_FLASH_CLOCK_48MHZ
#define CONFIG_IT83XX_VCC_1P8V

/* ADC channels */
#define ADC_TEMP_SNS_AMBIENT_CHANNEL CHIP_ADC_CH13
#define ADC_TEMP_SNS_DDR_CHANNEL CHIP_ADC_CH15
#define ADC_TEMP_SNS_SKIN_CHANNEL CHIP_ADC_CH6
#define ADC_TEMP_SNS_VR_CHANNEL CHIP_ADC_CH1

#ifdef CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
/* delay to turn on/off vconn */
#endif
#endif /* __CROS_EC_ITE_EC_H */
