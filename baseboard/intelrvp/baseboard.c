/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel-RVP family-specific configuration */

#include "adc_chip.h"
#include "charge_state.h"
#include "espi.h"
#include "fan.h"
#include "hooks.h"
#include "ioexpander_pca9555.h"
#include "peci.h"
#include "power.h"
#include "temp_sensor.h"
#include "thermistor.h"

/* GPIO for power signal */
#ifdef CONFIG_HOSTCMD_ESPI_VW_SLP_SIGNALS
#define SLP_S3_SIGNAL_L VW_SLP_S3_L
#define SLP_S4_SIGNAL_L VW_SLP_S4_L
#else
#define SLP_S3_SIGNAL_L GPIO_PCH_SLP_S3_L
#define SLP_S4_SIGNAL_L GPIO_PCH_SLP_S4_L
#endif

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_DEASSERTED] = {
		GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_DEASSERTED] = {
		SLP_S3_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		SLP_S4_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S4_DEASSERTED",
	},
	[X86_RSMRST_L_PGOOD] = {
		GPIO_RSMRST_L_PGOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"RSMRST_L_PGOOD",
	},
	[X86_ALL_SYS_PWRGD] = {
		GPIO_ALL_SYS_PWRGD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"ALL_SYS_PWRGD",
	},
#if defined(CONFIG_CHIPSET_ICELAKE)
	[X86_SLP_SUS_DEASSERTED] = {
		GPIO_PCH_SLP_SUS_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_SUS_DEASSERTED",
	},
	[X86_DSW_DPWROK] = {
		GPIO_DSW_DPWROK,
		POWER_SIGNAL_ACTIVE_HIGH,
		"DSW_DPWROK",
	},
#elif defined(CONFIG_CHIPSET_COMETLAKE)
	[PP5000_A_PGOOD] = {
		GPIO_PP5000_A_PG_OD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP5000_A_PGOOD",
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SNS_AMBIENT] = {
		.name = "ADC_TEMP_SNS_AMBIENT",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_AMBIENT_CHANNEL,
	},
	[ADC_TEMP_SNS_DDR] = {
		.name = "ADC_TEMP_SNS_DDR",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_DDR_CHANNEL,
	},
	[ADC_TEMP_SNS_SKIN] = {
		.name = "ADC_TEMP_SNS_SKIN",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_SKIN_CHANNEL,
	},
	[ADC_TEMP_SNS_VR] = {
		.name = "ADC_TEMP_SNS_VR",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_VR_CHANNEL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

#ifdef CONFIG_TEMP_SENSOR
/* Temperature sensors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SNS_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v0_22k6_47k_4050b,
		.idx = ADC_TEMP_SNS_AMBIENT,
		.action_delay_sec = 5,
	},
	[TEMP_SNS_BATTERY] = {
		.name = "Battery",
		.type = TEMP_SENSOR_TYPE_BATTERY,
		.read = charge_get_battery_temp,
		.idx = 0,
		.action_delay_sec = 1,
	},
	[TEMP_SNS_DDR] = {
		.name = "DDR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v0_22k6_47k_4050b,
		.idx = ADC_TEMP_SNS_DDR,
		.action_delay_sec = 1,
	},
#ifdef CONFIG_PECI
	[TEMP_SNS_PECI] = {
		.name = "PECI",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = peci_temp_sensor_get_val,
		.idx = 0,
		.action_delay_sec = 1,
	},
#endif /* CONFIG_PECI */
	[TEMP_SNS_SKIN] = {
		.name = "Skin",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v0_22k6_47k_4050b,
		.idx = ADC_TEMP_SNS_SKIN,
		.action_delay_sec = 1,
	},
	[TEMP_SNS_VR] = {
		.name = "VR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v0_22k6_47k_4050b,
		.idx = ADC_TEMP_SNS_VR,
		.action_delay_sec = 1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
#endif /* CONFIG_TEMP_SENSOR */

#ifdef CONFIG_FANS
/* Physical fan config */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 0,
	.pgood_gpio = GPIO_ALL_SYS_PWRGD,
	.enable_gpio = GPIO_FAN_POWER_EN,
};

/* Physical fan rpm config */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3100,
	.rpm_start = 3100,
	.rpm_max = 6900,
};

/* FAN channels */
struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SNS_AMBIENT] = thermal_a,
	[TEMP_SNS_BATTERY] = thermal_a,
	[TEMP_SNS_DDR] = thermal_a,
#ifdef CONFIG_PECI
	[TEMP_SNS_PECI] = thermal_a,
#endif
	[TEMP_SNS_SKIN] = thermal_a,
	[TEMP_SNS_VR] = thermal_a,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
#endif /* CONFIG_FANS */

static void board_init(void)
{
	/* Enable SOC SPI */
	gpio_set_level(GPIO_EC_SPI_OE_N, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_LAST);

int ioexpander_read_intelrvp_version(int *port0, int *port1)
{
	if (pca9555_read(I2C_PORT_PCA9555_BOARD_ID_GPIO,
		I2C_ADDR_PCA9555_BOARD_ID_GPIO,
		PCA9555_CMD_INPUT_PORT_0, port0))
		return -1;

	return pca9555_read(I2C_PORT_PCA9555_BOARD_ID_GPIO,
		I2C_ADDR_PCA9555_BOARD_ID_GPIO,
		PCA9555_CMD_INPUT_PORT_1, port1);
}
