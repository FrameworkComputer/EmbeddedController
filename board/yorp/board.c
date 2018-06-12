/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/bq24392.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p3483.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C1_MUX_INT_ODL) &&
	    !gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		nx20p3483_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		nx20p3483_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and GPIO interrupts*/
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_AMB] = {
		"TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	[ADC_TEMP_SENSOR_CHARGER] = {
		"TEMP_CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/*
 * Data derived from Seinhart-Hart equation in a resistor divider circuit with
 * Vdd=3300mV, R = 13.7Kohm, and Murata NCP15WB-series thermistor (B = 4050,
 * T0 = 298.15, nominal resistance (R0) = 47Kohm).
 */
#define CHARGER_THERMISTOR_SCALING_FACTOR 13
static const struct thermistor_data_pair charger_thermistor_data[] = {
	{ 3044 / CHARGER_THERMISTOR_SCALING_FACTOR, 0 },
	{ 2890 / CHARGER_THERMISTOR_SCALING_FACTOR, 10 },
	{ 2680 / CHARGER_THERMISTOR_SCALING_FACTOR, 20 },
	{ 2418 / CHARGER_THERMISTOR_SCALING_FACTOR, 30 },
	{ 2117 / CHARGER_THERMISTOR_SCALING_FACTOR, 40 },
	{ 1800 / CHARGER_THERMISTOR_SCALING_FACTOR, 50 },
	{ 1490 / CHARGER_THERMISTOR_SCALING_FACTOR, 60 },
	{ 1208 / CHARGER_THERMISTOR_SCALING_FACTOR, 70 },
	{ 966 / CHARGER_THERMISTOR_SCALING_FACTOR, 80 },
	{ 860 / CHARGER_THERMISTOR_SCALING_FACTOR, 85 },
	{ 766 / CHARGER_THERMISTOR_SCALING_FACTOR, 90 },
	{ 679 / CHARGER_THERMISTOR_SCALING_FACTOR, 95 },
	{ 603 / CHARGER_THERMISTOR_SCALING_FACTOR, 100 },
};

static const struct thermistor_info charger_thermistor_info = {
	.scaling_factor = CHARGER_THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(charger_thermistor_data),
	.data = charger_thermistor_data,
};

int board_get_charger_temp(int idx, int *temp_ptr)
{
	int mv = adc_read_channel(NPCX_ADC_CH1);

	if (mv < 0)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = thermistor_linear_interpolate(mv, &charger_thermistor_info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}

/*
 * Data derived from Seinhart-Hart equation in a resistor divider circuit with
 * Vdd=3300mV, R = 51.1Kohm, and Murata NCP15WB-series thermistor (B = 4050,
 * T0 = 298.15, nominal resistance (R0) = 47Kohm).
 */
#define AMB_THERMISTOR_SCALING_FACTOR 11
static const struct thermistor_data_pair amb_thermistor_data[] = {
	{ 2512 / AMB_THERMISTOR_SCALING_FACTOR, 0 },
	{ 2158 / AMB_THERMISTOR_SCALING_FACTOR, 10 },
	{ 1772 / AMB_THERMISTOR_SCALING_FACTOR, 20 },
	{ 1398 / AMB_THERMISTOR_SCALING_FACTOR, 30 },
	{ 1070 / AMB_THERMISTOR_SCALING_FACTOR, 40 },
	{ 803 / AMB_THERMISTOR_SCALING_FACTOR, 50 },
	{ 597 / AMB_THERMISTOR_SCALING_FACTOR, 60 },
	{ 443 / AMB_THERMISTOR_SCALING_FACTOR, 70 },
	{ 329 / AMB_THERMISTOR_SCALING_FACTOR, 80 },
	{ 285 / AMB_THERMISTOR_SCALING_FACTOR, 85 },
	{ 247 / AMB_THERMISTOR_SCALING_FACTOR, 90 },
	{ 214 / AMB_THERMISTOR_SCALING_FACTOR, 95 },
	{ 187 / AMB_THERMISTOR_SCALING_FACTOR, 100 },
};

static const struct thermistor_info amb_thermistor_info = {
	.scaling_factor = AMB_THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(amb_thermistor_data),
	.data = amb_thermistor_data,
};

int board_get_ambient_temp(int idx, int *temp_ptr)
{
	int mv = adc_read_channel(NPCX_ADC_CH0);

	if (mv < 0)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = thermistor_linear_interpolate(mv, &amb_thermistor_info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}

const struct temp_sensor_t temp_sensors[] = {
	{
		.name = "Battery",
		.type = TEMP_SENSOR_TYPE_BATTERY,
		.read = charge_get_battery_temp,
		.idx = 0,
		.action_delay_sec = 1
	},
	{
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_ambient_temp,
		.idx = 0,
		.action_delay_sec = 5
	},
	{
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_charger_temp,
		.idx = 1,
		.action_delay_sec = 1
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, 0,  FLOAT_TO_FP(1)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct stprivate_data lsm6dsm_g_data;
static struct stprivate_data lsm6dsm_a_data;

/* Drivers */
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
	 .addr = KX022_ADDR1,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 4, /* g */
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		 /* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &lsm6dsm_a_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = LSM6DSM_ADDR0,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g */
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &lsm6dsm_g_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = LSM6DSM_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Initialize board. */
static void board_init(void)
{
	/* Enable Base Accel interrupt */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_hibernate_late(void) {

	int i;

	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs before going to hibernate */
		{GPIO_BAT_LED_BLUE_L, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_ORANGE_L, GPIO_INPUT | GPIO_PULL_UP},
	};

	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);
}

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif

/* Sets the gpio flags correct taking into account warm resets */
static void reset_gpio_flags(enum gpio_signal signal, int flags)
{
	/*
	 * If the system was already on, we cannot set the value otherwise we
	 * may change the value from the previous image which could cause a
	 * brownout.
	 */
	if (system_is_reboot_warm() || system_jumped_to_this_image())
		flags &= ~(GPIO_LOW | GPIO_HIGH);

	gpio_set_flags(signal, flags);
}

/* Runtime GPIO defaults */
enum gpio_signal gpio_sys_reset_l_runtime = GPIO_SYS_RESET_L_V1;
enum gpio_signal gpio_entering_rw_runtime = GPIO_ENTERING_RW_V1;
enum gpio_signal gpio_usb2_otg_id_runtime = GPIO_USB2_OTG_ID_V1;

static void update_drivers_from_board_id(void)
{
	uint32_t board_id = 0;

	/* Errors will count as board_id 0 */
	cbi_get_board_version(&board_id);

	if (board_id == 0) {
		/* EC drives C1 PPC for proto 1. b/78896495 */
		reset_gpio_flags(GPIO_WFCAM_VSYNC, GPIO_OUT_LOW);
		reset_gpio_flags(GPIO_C1_EN_SNK_V0, GPIO_OUT_LOW);
		ppc_chips[1].flags = PPC_CFG_FLAGS_GPIO_CONTROL;
		ppc_chips[1].snk_gpio = GPIO_C1_EN_SNK_V0;  /* GPIO40 */
		ppc_chips[1].src_gpio = GPIO_WFCAM_VSYNC;   /* GPIO03 */

		/*
		 * We don't have to worry about changing USB-A port count from
		 * 2 -> 1 since GPIO_EN_USB_A1_5V on V0 board just drives the
		 * EC_BRD_ID_EN signal (which will burn ~ 400uW of power)
		 */

		/* Move the runtime GPIOS */
		reset_gpio_flags(GPIO_SYS_RESET_L_V0, GPIO_ODR_HIGH);
		gpio_sys_reset_l_runtime = GPIO_SYS_RESET_L_V0;     /* GPIO02 */

		reset_gpio_flags(GPIO_ENTERING_RW_V0, GPIO_OUT_LOW);
		gpio_entering_rw_runtime = GPIO_ENTERING_RW_V0;     /* GPIO80 */

		reset_gpio_flags(GPIO_USB_A1_CHARGE_EN_L, GPIO_OUT_LOW);
		gpio_usb2_otg_id_runtime = GPIO_USB_A1_CHARGE_EN_L; /* GPIOA0 */
	}
	if (board_id >= 1) {
		/* Reset flags to correct state instead of GPIO_DEFAULT */
		reset_gpio_flags(GPIO_SYS_RESET_L_V1, GPIO_ODR_HIGH);
		reset_gpio_flags(GPIO_ENTERING_RW_V1, GPIO_OUT_LOW);
		reset_gpio_flags(GPIO_USB_A1_CHARGE_EN_L, GPIO_OUT_HIGH);
		reset_gpio_flags(GPIO_USB2_OTG_ID_V1, GPIO_OUT_LOW);
		reset_gpio_flags(GPIO_WFCAM_VSYNC, GPIO_INPUT);
	}
}
DECLARE_HOOK(HOOK_INIT, update_drivers_from_board_id, HOOK_PRIO_INIT_I2C + 1);
