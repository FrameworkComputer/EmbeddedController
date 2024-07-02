/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* metaknight board-specific configuration */

#include "adc.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/nb7v904m.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "stdbool.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

#define ADC_VOL_UP_MASK BIT(0)
#define ADC_VOL_DOWN_MASK BIT(1)

static uint8_t new_adc_key_state;

/* USB-A Configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_VBUS,
	GPIO_EN_USB_A1_VBUS,
};

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	/*
	 * The interrupt line is shared between the TCPC and BC 1.2 detection
	 * chip.  Therefore we'll need to check both ICs.
	 */
	schedule_deferred_pd_interrupt(0);
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

static void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

static void sub_hdmi_hpd_interrupt(enum gpio_signal s)
{
	int hdmi_hpd_odl = gpio_get_level(GPIO_HDMI_HPD_SUB_ODL);

	gpio_set_level(GPIO_EC_AP_USB_C1_HDMI_HPD, !hdmi_hpd_odl);

	cprints(CC_SYSTEM, "HDMI plug-%s", !hdmi_hpd_odl ? "in" : "out");
}

/**
 * Handle debounced pen input changing state.
 */
static void pen_input_deferred(void)
{
	int pen_charge_enable = !gpio_get_level(GPIO_PEN_DET_ODL) &&
				!chipset_in_state(CHIPSET_STATE_ANY_OFF);

	if (pen_charge_enable)
		gpio_set_level(GPIO_EN_PP3300_PEN, 1);
	else
		gpio_set_level(GPIO_EN_PP3300_PEN, 0);

	CPRINTS("Pen charge %sable", pen_charge_enable ? "en" : "dis");
}
DECLARE_DEFERRED(pen_input_deferred);

void pen_input_interrupt(enum gpio_signal signal)
{
	/* pen input debounce time */
	hook_call_deferred(&pen_input_deferred_data, (100 * MSEC));
}

static void pen_charge_check(void)
{
	hook_call_deferred(&pen_input_deferred_data, (100 * MSEC));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pen_charge_check, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pen_charge_check, HOOK_PRIO_LAST);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_SENSOR1",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_SENSOR2",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SUB_ANALOG] = {
		.name = "SUB_ANALOG",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_VSNS_PP3300_A] = {
		.name = "PP3300_A_PGOOD",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {

	[TEMP_SENSOR_MEMORY] = { .name = "Memory",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_51k1_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_CPU] = { .name = "CPU",
			      .type = TEMP_SENSOR_TYPE_BOARD,
			      .read = get_temp_3v3_51k1_47k_4050b,
			      .idx = ADC_TEMP_SENSOR_2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_memory = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
};

const static struct ec_thermal_config thermal_cpu = {
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
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_thermal(void)
{
	thermal_params[TEMP_SENSOR_MEMORY] = thermal_memory;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}

void board_hibernate(void)
{
	/*
	 * Both charger ICs need to be put into their "low power mode" before
	 * entering the Z-state.
	 */
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(1, true);
	raa489000_hibernate(0, true);
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b:147316511): Here we could issue a digital reset to the IC,
	 * unsure if we actually want to do that or not yet.
	 */
}

#ifdef BOARD_WADDLEDOO
static void reconfigure_5v_gpio(void)
{
	/*
	 * b/147257497: On early waddledoo boards, GPIO_EN_PP5000 was swapped
	 * with GPIO_VOLUP_BTN_ODL. Therefore, we'll actually need to set that
	 * GPIO instead for those boards.  Note that this breaks the volume up
	 * button functionality.
	 */
	if (system_get_board_version() < 0) {
		CPRINTS("old board - remapping 5V en");
		gpio_set_flags(GPIO_VOLUP_BTN_ODL, GPIO_OUT_LOW);
	}
}
DECLARE_HOOK(HOOK_INIT, reconfigure_5v_gpio, HOOK_PRIO_INIT_I2C + 1);
#endif /* BOARD_WADDLEDOO */

static void set_5v_gpio(int level)
{
	int version;
	enum gpio_signal gpio = GPIO_EN_PP5000;

	/*
	 * b/147257497: On early waddledoo boards, GPIO_EN_PP5000 was swapped
	 * with GPIO_VOLUP_BTN_ODL. Therefore, we'll actually need to set that
	 * GPIO instead for those boards.  Note that this breaks the volume up
	 * button functionality.
	 */
	if (IS_ENABLED(BOARD_WADDLEDOO)) {
		version = system_get_board_version();

		/*
		 * If the CBI EEPROM wasn't formatted, assume it's a very early
		 * board.
		 */
		gpio = version < 0 ? GPIO_VOLUP_BTN_ODL : GPIO_EN_PP5000;
	}

	gpio_set_level(gpio, level);
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Port 0 simply has a GPIO to turn on the 5V regulator, however, 5V is
	 * generated locally on the sub board and we need to set the comparator
	 * polarity on the sub board charger IC, or send enable signal to HDMI
	 * DB.
	 */
	set_5v_gpio(!!enable);

	if (get_cbi_fw_config_db() == DB_1A_HDMI ||
	    get_cbi_fw_config_db() == DB_LTE_HDMI) {
		gpio_set_level(GPIO_SUB_C1_INT_EN_RAILS_ODL, !enable);
	}
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override uint8_t board_get_charger_chip_count(void)
{
	return CHARGER_NUM;
}

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		if (tcpc_write(i, TCPC_REG_COMMAND,
			       TCPC_REG_COMMAND_SNK_CTRL_LOW))
			CPRINTS("p%d: sink path disable failed.", i);
		raa489000_enable_asgate(i, false);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (port < 0 || port > board_get_usb_pd_port_count())
		return;

	raa489000_set_output_current(port, rp);
}

/* Sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrices to rotate accelerometers into the standard reference. */
static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					      { FLOAT_TO_FP(1), 0, 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_lsm6dsm_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_icm_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					 { 0, FLOAT_TO_FP(1), 0 },
					 { 0, 0, FLOAT_TO_FP(-1) } };

static struct accelgyro_saved_data_t g_bma253_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct kionix_accel_data g_kx022_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;
static struct icm_drv_data_t g_icm426xx_data;

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma253_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
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
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
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
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

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
	.default_range = 2, /* g, to support tablet mode */
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

struct motion_sensor_t lsm6dsm_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DS3,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex,
	.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.rot_standard_ref = &base_lsm6dsm_ref,
	.default_range = 4,  /* g */
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
	.config = {
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
	},
};

struct motion_sensor_t lsm6dsm_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DS3,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex,
	.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data, MOTIONSENSE_TYPE_GYRO),
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.default_range = 1000 | ROUND_UP_FLAG, /* dps */
	.rot_standard_ref = &base_lsm6dsm_ref,
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
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
	.default_range = 4, /* g, enough for laptop */
	.rot_standard_ref = &base_icm_ref,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
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

static int base_gyro_config;

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	check_c0_line();

	if (get_cbi_fw_config_db() == DB_1A_HDMI ||
	    get_cbi_fw_config_db() == DB_LTE_HDMI) {
		/* Disable i2c on HDMI pins */
		gpio_config_pin(MODULE_I2C, GPIO_HDMI_HPD_SUB_ODL, 0);
		gpio_config_pin(MODULE_I2C, GPIO_GPIO92_NC, 0);

		gpio_set_flags(GPIO_SUB_C1_INT_EN_RAILS_ODL, GPIO_ODR_HIGH);

		/* Select HDMI option */
		gpio_set_level(GPIO_HDMI_SEL_L, 0);

		/* Enable interrupt for passing through HPD */
		gpio_enable_interrupt(GPIO_HDMI_HPD_SUB_ODL);
	} else {
		/* Set SDA as an input */
		gpio_set_flags(GPIO_HDMI_HPD_SUB_ODL, GPIO_INPUT);
	}
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);

	/* Enable gpio interrupt for pen detect */
	gpio_enable_interrupt(GPIO_PEN_DET_ODL);

	/* Turn on 5V if the system is on, otherwise turn it off. */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	/* Initialize g-sensor */
	base_gyro_config = get_cbi_ssfc_base_sensor();

	if (base_gyro_config == SSFC_SENSOR_LSM6DSM) {
		motion_sensors[BASE_ACCEL] = lsm6dsm_base_accel;
		motion_sensors[BASE_GYRO] = lsm6dsm_base_gyro;
		cprints(CC_SYSTEM, "SSFC: BASE GYRO is LSM6DSM");
	} else if (get_cbi_ssfc_base_sensor() == SSFC_SENSOR_ICM426XX) {
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
		cprints(CC_SYSTEM, "SSFC: BASE GYRO is ICM426XX");
	} else
		cprints(CC_SYSTEM, "SSFC: BASE GYRO is BMI160");

	if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_KX022) {
		motion_sensors[LID_ACCEL] = kx022_lid_accel;
		cprints(CC_SYSTEM, "SSFC: LID ACCEL is KX022");
	} else
		cprints(CC_SYSTEM, "SSFC: LID ACCEL is BMA253");

	/* Initial thermal */
	setup_thermal();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
		.flags = PI3USB9201_ALWAYS_POWERED,
	},
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
		.alert_signal = GPIO_USB_C0_INT_ODL,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
			},
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;
	int p;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2
	 * detector IC. Therefore, go out and actually read the alert
	 * registers to report the alert status.
	 */
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		if (gpio_get_level(tcpc_config[p].alert_signal) ||
		    tcpc_read16(p, TCPC_REG_ALERT, &regval))
			continue;
		/* The TCPCI Rev 1.0 spec says to ignore bits 14:12. */
		if (!(tcpc_config[p].flags & TCPC_FLAGS_TCPCI_REV2_0))
			regval &= ~(BIT(14) | BIT(13) | BIT(12));
		if (regval)
			status |= (PD_STATUS_TCPC_ALERT_0 << p);
	}

	return status;
}

int adc_to_physical_value(enum gpio_signal gpio)
{
	if (gpio == GPIO_VOLUME_UP_L)
		return !!(new_adc_key_state & ADC_VOL_UP_MASK);
	else if (gpio == GPIO_VOLUME_DOWN_L)
		return !!(new_adc_key_state & ADC_VOL_DOWN_MASK);

	CPRINTS("Not a volume up or down key");
	return 0;
}

int button_is_adc_detected(enum gpio_signal gpio)
{
	return (gpio == GPIO_VOLUME_DOWN_L) || (gpio == GPIO_VOLUME_UP_L);
}

static void adc_vol_key_press_check(void)
{
	int volt = adc_read_channel(ADC_SUB_ANALOG);
	static uint8_t old_adc_key_state;
	uint8_t adc_key_state_change;

	if (volt > 2400 && volt < 2540) {
		/* volume-up is pressed */
		new_adc_key_state = ADC_VOL_UP_MASK;
	} else if (volt > 2600 && volt < 2740) {
		/* volume-down is pressed */
		new_adc_key_state = ADC_VOL_DOWN_MASK;
	} else if (volt < 2300) {
		/* both volumn-up and volume-down are pressed */
		new_adc_key_state = ADC_VOL_UP_MASK | ADC_VOL_DOWN_MASK;
	} else if (volt > 2780) {
		/* both volumn-up and volume-down are released */
		new_adc_key_state = 0;
	}
	if (new_adc_key_state != old_adc_key_state) {
		adc_key_state_change = old_adc_key_state ^ new_adc_key_state;
		if (adc_key_state_change & ADC_VOL_UP_MASK)
			button_interrupt(GPIO_VOLUME_UP_L);
		if (adc_key_state_change & ADC_VOL_DOWN_MASK)
			button_interrupt(GPIO_VOLUME_DOWN_L);

		old_adc_key_state = new_adc_key_state;
	}
}
DECLARE_HOOK(HOOK_TICK, adc_vol_key_press_check, HOOK_PRIO_DEFAULT);

#ifndef TEST_BUILD
void motion_interrupt(enum gpio_signal signal)
{
	switch (base_gyro_config) {
	case SSFC_SENSOR_LSM6DSM:
		lsm6dsm_interrupt(signal);
		break;
	case SSFC_SENSOR_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case SSFC_SENSOR_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

const struct i2c_port_t i2c_ports[] = {
	{ .name = "eeprom",
	  .port = I2C_PORT_EEPROM,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_EEPROM_SCL,
	  .sda = GPIO_EC_I2C_EEPROM_SDA },

	{ .name = "battery",
	  .port = I2C_PORT_BATTERY,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C_BATTERY_SCL,
	  .sda = GPIO_EC_I2C_BATTERY_SDA },

	{ .name = "sensor",
	  .port = I2C_PORT_SENSOR,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_SENSOR_SCL,
	  .sda = GPIO_EC_I2C_SENSOR_SDA },

	{ .name = "usbc0",
	  .port = I2C_PORT_USB_C0,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_USB_C0_SCL,
	  .sda = GPIO_EC_I2C_USB_C0_SDA },
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	{ .name = "sub_usbc1",
	  .port = I2C_PORT_SUB_USB_C1,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_SUB_USB_C1_SCL,
	  .sda = GPIO_EC_I2C_SUB_USB_C1_SDA },
#endif
};

#endif
