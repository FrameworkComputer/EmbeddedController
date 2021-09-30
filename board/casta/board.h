/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Casta board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_TCPC_0_PS8751
#define VARIANT_OCTOPUS_NO_SENSORS
#define CONFIG_CHARGER_RUNTIME_CONFIG
#include "baseboard.h"

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

#define CONFIG_LED_COMMON

/* USB PD */
#undef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
/*
 * This board configures two chargers, one of which can measure VBUS and one of
 * which cannot. Leave the default config, which defines
 * CONFIG_USB_PD_VBUS_MEASURE_CHARGER.
 */

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR

/*
 * Don't allow the system to boot to S0 when the battery is low and unable to
 * communicate on locked systems (which haven't PD negotiated).
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000

#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15001

/*
 * Allow an additional second during power button init to let PD negotiation
 * complete when we have no battery and need to meet
 * CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON.  SKUs which do not have a TCPC on
 * port 1 will take slightly longer to complete negotiation while the PD1 task
 * attempts to communicate with its TCPC before suspending.
 */
#undef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 2

/* Keyboard Backlight is unconnected in casta proto */
#undef CONFIG_PWM
#undef CONFIG_PWM_KBLIGHT

/* All casta systems are clamshells */
#undef CONFIG_TABLET_MODE

/* TODO(b/119872005): Casta: confirm thermistor parts */
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* Battery W/A */
#define CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA 6144
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_I2C_XFER_BOARD_CALLBACK

/* The board needs 100ms for VBUS_C[0|1]_BC12 to drop to lower VvbusUVLO */
#undef CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS
#define CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS 100

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB,		/* ADC0 */
	ADC_TEMP_SENSOR_CHARGER,	/* ADC1 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_SDI,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
