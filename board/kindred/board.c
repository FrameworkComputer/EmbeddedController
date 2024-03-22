/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch board-specific configuration */

#include "adc.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "stdbool.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static int lid_device_id;
static int base_device_id;

static void check_reboot_deferred(void);
DECLARE_DEFERRED(check_reboot_deferred);

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 },   { 1, 0 },	{ 0, 6 },   { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 },   { 1, 3 },	{ -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 },   { 1, 5 },	{ 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 },   { 1, 2 },	{ 2, 3 },   { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
	{ -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		sn5s330_interrupt(0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		sn5s330_interrupt(1);
		break;

	default:
		break;
	}
}

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	baseboard_mst_enable_control(MST_HDMI, gpio_get_level(signal));
}

static void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	[PWM_CH_FAN] = { .channel = 5,
			 .flags = PWM_CONFIG_OPEN_DRAIN,
			 .freq = 25000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		.flags = TCPC_FLAGS_RESET_ACTIVE_HIGH,
	},
	[USB_PD_PORT_TCPC_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		.flags = 0,
	},
};

static int board_anx7447_mux_set_c0(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int port = me->usb_port;
	int rv = EC_SUCCESS;

	if (port != USB_PD_PORT_TCPC_0)
		return rv;

	if (gpio_get_level(GPIO_CCD_MODE_ODL))
		return rv;

	/*
	 * Expect to set AUX_SWITCH to 0, but 0xc isolates the DP_AUX
	 * signal from SBU.
	 */
	CPRINTS("C%d: AUX_SW_SEL=0x%x", port, 0xc);
	if (tcpc_write(port, ANX7447_REG_TCPC_AUX_SWITCH, 0xc))
		CPRINTS("C%d: Setting AUX_SW_SEL failed", port);

	return rv;
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
			.driver = &anx7447_usb_mux_driver,
			.board_set = &board_anx7447_mux_set_c0,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
		},
	},
	[USB_PD_PORT_TCPC_1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_1,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	}
};

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},

	[USB_PD_PORT_TCPC_1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/******************************************************************************/
/* Sensors */
/* Base Sensor mutex */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

/* Base accel private data */
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;

/* BMA255 private data */
static struct accelgyro_saved_data_t g_bma255_data;
static struct kionix_accel_data g_kx022_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(-1), 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_icm_ref = { { 0, FLOAT_TO_FP(1), 0 },
					 { FLOAT_TO_FP(-1), 0, 0 },
					 { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t kx022_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_kx022_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = KX022_ADDR0_FLAGS,
	.rot_standard_ref = &lid_standard_ref,
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 2, /* g, enough for laptop */
	.rot_standard_ref = &base_icm_ref,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
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
};

struct motion_sensor_t icm426xx_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_icm_ref,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma255_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support lid angle calculation. */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* Sensor on in S3 */
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
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100,
			},
			/* Sensor on in S3 */
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
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

/* Default */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3200,
	.rpm_start = 3200,
	.rpm_max = 6500,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN },
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = { "TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_2] = { "TEMP_CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_3] = { "TEMP_WIFI", NPCX_ADC_CH3, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Temp1",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Temp2",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Temp3",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Hatch Temperature sensors */
/*
 * TODO(b/124316213): These setting need to be reviewed and set appropriately
 * for Hatch. They matter when the EC is controlling the fan as opposed to DPTF
 * control.
 */
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = C_TO_K(75),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(55),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(55),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_1] = thermal_a;
	thermal_params[TEMP_SENSOR_2] = thermal_a;
}

/* Sets the gpio flags correct taking into account warm resets */
static void reset_gpio_flags(enum gpio_signal signal, int flags)
{
	/*
	 * If the system was already on, we cannot set the value otherwise we
	 * may change the value from the previous image which could cause a
	 * brownout.
	 */
	if (system_is_reboot_warm() || system_jumped_late())
		flags &= ~(GPIO_LOW | GPIO_HIGH);

	gpio_set_flags(signal, flags);
}

/* Runtime GPIO defaults */
enum gpio_signal gpio_en_pp5000_a = GPIO_EN_PP5000_A_V1;

static void board_gpio_set_pp5000(void)
{
	uint32_t board_id = 0;

	/* Errors will count as board_id 0 */
	cbi_get_board_version(&board_id);

	if (board_id == 0) {
		reset_gpio_flags(GPIO_EN_PP5000_A_V0, GPIO_OUT_LOW);
		/* Change runtime default for V0 */
		gpio_en_pp5000_a = GPIO_EN_PP5000_A_V0;
	} else if (board_id >= 1) {
		reset_gpio_flags(GPIO_EN_PP5000_A_V1, GPIO_OUT_LOW);
	}
}

bool board_is_convertible(void)
{
	uint8_t sku_id = get_board_sku();
	/* SKU ID of Kled : 1, 2, 3, 4 */
	return (sku_id >= 1) && (sku_id <= 4);
}

static void board_update_sensor_config_from_sku(void)
{
	/*
	 * There are two possible sensor configurations. Clamshell device will
	 * not have any of the motion sensors populated, while convertible
	 * devices have the BMI160 Accel/Gryo lid acceleration sensor.
	 * If a new SKU id is used that is not in the threshold, then the
	 * number of motion sensors will remain as ARRAY_SIZE(motion_sensors).
	 */
	if (board_is_convertible()) {
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable gpio interrupt for base accelgyro sensor */
		gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);

		CPRINTS("Motion Sensor Count = %d", motion_sensor_count);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static void board_init(void)
{
	/* Initialize Fans */
	setup_fans();
	/* Enable HDMI HPD interrupt. */
	gpio_enable_interrupt(GPIO_HDMI_CONN_HPD);
	/* Select correct gpio signal for PP5000_A control */
	board_gpio_set_pp5000();
	/* Use sku_id to set motion sensor count */
	board_update_sensor_config_from_sku();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void determine_accel_devices(void)
{
	static uint8_t read_time;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	if (read_time == 0 && board_is_convertible()) {
		/* Read g sensor chip id*/
		i2c_read8(I2C_PORT_ACCEL, KX022_ADDR0_FLAGS, KX022_WHOAMI,
			  &lid_device_id);
		/* Read gyro sensor id*/
		i2c_read8(I2C_PORT_ACCEL, ICM426XX_ADDR0_FLAGS,
			  ICM426XX_REG_WHO_AM_I, &base_device_id);

		CPRINTS("Motion Sensor Base id = %d Lid id =%d", base_device_id,
			lid_device_id);

		if (lid_device_id == KX022_WHO_AM_I_VAL) {
			motion_sensors[LID_ACCEL] = kx022_lid_accel;
			ccprints("Lid Accel is KX022");
		} else
			ccprints("Lid Accel is BMA255");

		if (base_device_id == ICM426XX_CHIP_ICM40608) {
			motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
			motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
			ccprints("BASE Accel is ICM426XX");
		} else
			ccprints("BASE Accel is BMI160");

		read_time++;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, determine_accel_devices, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, determine_accel_devices, HOOK_PRIO_INIT_ADC + 2);

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_device_id) {
	case ICM426XX_CHIP_ICM40608:
		icm426xx_interrupt(signal);
		break;
	default:
		bmi160_interrupt(signal);
		break;
	}
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC_ODL, !is_overcurrented);
}

bool board_has_kb_backlight(void)
{
	uint8_t sku_id = get_board_sku();
	/* SKU ID of Kled with KB backlight: 1, 2, 3, 4 */
	return (sku_id >= 1) && (sku_id <= 4);
}

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	if (board_has_kb_backlight())
		return flags0;
	else
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
}

void all_sys_pgood_check_reboot(void)
{
	hook_call_deferred(&check_reboot_deferred_data, 3000 * MSEC);
}

__override void board_chipset_forced_shutdown(void)
{
	hook_call_deferred(&check_reboot_deferred_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_forced_shutdown,
	     HOOK_PRIO_DEFAULT);

static void check_reboot_deferred(void)
{
	if (!gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD))
		system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
}
