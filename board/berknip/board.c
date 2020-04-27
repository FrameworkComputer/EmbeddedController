/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Berknip board configuration */

#include "button.h"
#include "cbi_ec_fw_config.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/retimer/ps8811.h"
#include "driver/usb_mux/amd_fp5.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_mux.h"

#include "gpio_list.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KX022,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kx022_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 2, /* g, enough for laptop */
	 .rot_standard_ref = NULL,
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = NULL,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#endif /* HAS_TASK_MOTIONSENSE */

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_FAN] = {
		.channel = 2,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/*****************************************************************************
 * USB-A Retimer tuning
 */
#define PS8811_ACCESS_RETRIES 2

/* PS8811 gain tuning */
static void ps8811_tuning_init(void)
{
	int rv;
	int retry;

	/* Turn on the retimers */
	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 1);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);

	/* USB-A0 can run with default settings */
	for (retry = 0; retry < PS8811_ACCESS_RETRIES; ++retry) {
		int val;

		rv = i2c_read8(I2C_PORT_USBA0,
				PS8811_I2C_ADDR_FLAGS + PS8811_REG_PAGE1,
				PS8811_REG1_USB_BEQ_LEVEL, &val);
		if (!rv)
			break;
	}
	if (rv) {
		ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);
		CPRINTSUSB("C0: PS8811 not detected");
	}

	/* USB-A1 needs to increase gain to get over MB/DB connector */
	for (retry = 0; retry < PS8811_ACCESS_RETRIES; ++retry) {
		rv = i2c_write8(I2C_PORT_USBA1,
				PS8811_I2C_ADDR_FLAGS + PS8811_REG_PAGE1,
				PS8811_REG1_USB_BEQ_LEVEL,
				PS8811_BEQ_I2C_LEVEL_UP_13DB |
				PS8811_BEQ_PIN_LEVEL_UP_18DB);
		if (!rv)
			break;
	}
	if (rv) {
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
		CPRINTSUSB("C1: PS8811 not detected");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, ps8811_tuning_init, HOOK_PRIO_DEFAULT);

static void ps8811_retimer_off(void)
{
	/* Turn on the retimers */
	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, ps8811_retimer_off, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */
static void setup_mux(void)
{
	if (ec_config_has_usbc1_retimer_ps8802()) {
		ccprints("C1 PS8802 detected");

		/*
		 * Main MUX is PS8802, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8802
		 * table entry.
		 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_ps8802,
		       sizeof(struct usb_mux));

		/* Set the AMD FP5 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_amd_fp5_usb_mux;

		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	} else if (ec_config_has_usbc1_retimer_ps8818()) {
		ccprints("C1 PS8818 detected");

		/*
		 * Main MUX is FP5, secondary MUX is PS8818
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_amd_fp5_usb_mux,
		       sizeof(struct usb_mux));

		/* Set the PS8818 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_ps8818;
	}
}

/* TODO(b:151232257) Remove probe code when hardware supports CBI */
#include "driver/retimer/ps8802.h"
#include "driver/retimer/ps8818.h"
static void probe_setup_mux_backup(void)
{
	if (usb_muxes[USBC_PORT_C1].driver != NULL)
		return;

	/*
	 * Identifying a PS8818 is faster than the PS8802,
	 * so do it first.
	 */
	if (ps8818_detect(&usbc1_ps8818) == EC_SUCCESS) {
		set_cbi_fw_config(0x00004000);
		setup_mux();
	} else if (ps8802_detect(&usbc1_ps8802) == EC_SUCCESS) {
		set_cbi_fw_config(0x00004001);
		setup_mux();
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, probe_setup_mux_backup, HOOK_PRIO_DEFAULT);

const struct pi3dpx1207_usb_control pi3dpx1207_controls[] = {
	[USBC_PORT_C0] = {
		.enable_gpio = IOEX_USB_C0_DATA_EN,
		.dp_enable_gpio = GPIO_USB_C0_IN_HPD,
	},
	[USBC_PORT_C1] = {
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3dpx1207_controls) == USBC_PORT_COUNT);

const struct usb_mux usbc0_pi3dpx1207_usb_retimer = {
	.usb_port = USBC_PORT_C0,
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = PI3DPX1207_I2C_ADDR_FLAGS,
	.driver = &pi3dpx1207_usb_retimer,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
		.next_mux = &usbc0_pi3dpx1207_usb_retimer,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */

void setup_fw_config(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	setup_mux();
}
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

/*****************************************************************************
 * Fan
 */

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3100,
	.rpm_start = 3100,
	.rpm_max = 6900,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

const static struct ec_thermal_config thermal_thermistor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);
