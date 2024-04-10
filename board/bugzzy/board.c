/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "cros_board_info.h"
#include "driver/accel_lis2ds.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_8042.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
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

/* C1 interrupt line shared by BC 1.2, TCPC, and charger */
static void check_c1_line(void);
DECLARE_DEFERRED(check_c1_line);

static void notify_c1_chips(void)
{
	schedule_deferred_pd_interrupt(1);
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

static void check_c1_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips.
	 */
	if (!gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void sub_usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}

static void sub_hdmi_hpd_interrupt(enum gpio_signal s)
{
	int hdmi_hpd_odl = gpio_get_level(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);

	gpio_set_level(GPIO_EC_AP_USB_C1_HDMI_HPD, !hdmi_hpd_odl);
}

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
	[ADC_TEMP_SENSOR_3] = {
		.name = "TEMP_SENSOR3",
		.input_ch = NPCX_ADC_CH5,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4] = {
		.name = "TEMP_SENSOR4",
		.input_ch = NPCX_ADC_CH6,
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
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Skin1",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
	[TEMP_SENSOR_4] = { .name = "Skin2",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_4 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static int board_id = -1;

void board_init(void)
{
	int on;

	/* Enable C0 interrupt and check if it needs processing */
	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	check_c0_line();

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		/* Disable i2c on HDMI pins */
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL,
				0);
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
				0);

		/* Set HDMI and sub-rail enables to output */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
			       chipset_in_state(CHIPSET_STATE_ON) ?
				       GPIO_ODR_LOW :
				       GPIO_ODR_HIGH);
		gpio_set_flags(GPIO_SUB_C1_INT_EN_RAILS_ODL, GPIO_ODR_HIGH);

		/* Select HDMI option */
		gpio_set_level(GPIO_HDMI_SEL_L, 0);

		/* Enable interrupt for passing through HPD */
		gpio_enable_interrupt(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);
	} else {
		/* Set SDA as an input */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL, GPIO_INPUT);

		/* Enable C1 interrupt and check if it needs processing */
		gpio_enable_interrupt(GPIO_SUB_C1_INT_EN_RAILS_ODL);
		check_c1_line();
	}
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);

	/* Turn on 5V if the system is on, otherwise turn it off. */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Enable HDMI any time the SoC is on */
static void hdmi_enable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, hdmi_enable, HOOK_PRIO_DEFAULT);

static void hdmi_disable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hdmi_disable, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Both charger ICs need to be put into their "low power mode" before
	 * entering the Z-state.
	 */
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(1, true);
	raa489000_hibernate(0, false);
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b:147316511): Here we could issue a digital reset to the IC,
	 * unsure if we actually want to do that or not yet.
	 */
}

static void set_5v_gpio(int level)
{
	gpio_set_level(GPIO_EN_PP5000, level);
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

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		gpio_set_level(GPIO_SUB_C1_INT_EN_RAILS_ODL, !enable);
	} else {
		if (isl923x_set_comparator_inversion(1, !!enable))
			CPRINTS("Failed to %sable sub rails!",
				enable ? "en" : "dis");
	}
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override uint8_t board_get_charger_chip_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CHARGER_NUM - 1;
	else
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

static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Matrices to rotate accelerometers into the standard reference. */
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
					      { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_standard_ref_lsm = { { FLOAT_TO_FP(1), 0, 0 },
						  { 0, FLOAT_TO_FP(-1), 0 },
						  { 0, 0, FLOAT_TO_FP(-1) } };

struct motion_sensor_t ldm6dsm_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex,
	.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.rot_standard_ref = &base_standard_ref_lsm,
	.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
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
};
struct motion_sensor_t ldm6dsm_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex,
	.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data, MOTIONSENSE_TYPE_GYRO),
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.default_range = 1000 | ROUND_UP_FLAG, /* dps */
	.rot_standard_ref = &base_standard_ref_lsm,
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,

};

static struct stprivate_data g_lis2ds_data;
static struct bmi_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DS,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2ds_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2ds_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DS_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = LIS2DS_ODR_MIN_VAL,
		.max_frequency = LIS2DS_ODR_MAX_VAL,
		.default_range = 2, /* g, to support lid angle calculation. */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 12500 | ROUND_UP_FLAG,
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

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI160 = 1,
	BASE_GYRO_LSM6DSM = 2,
};

static enum base_accelgyro_type base_accelgyro_config;
static void board_set_motionsensor(void)
{
	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
	}

	base_accelgyro_config = BASE_GYRO_BMI160;
	if (board_id > 6) {
		motion_sensors[BASE_ACCEL] = ldm6dsm_base_accel;
		motion_sensors[BASE_GYRO] = ldm6dsm_base_gyro;
		base_accelgyro_config = BASE_GYRO_LSM6DSM;
	}
}
DECLARE_HOOK(HOOK_INIT, board_set_motionsensor, HOOK_PRIO_INIT_I2C + 2);

void motion_interrupt(enum gpio_signal signal)
{
	if (base_accelgyro_config == BASE_GYRO_BMI160)
		bmi160_interrupt(signal);
	else
		lsm6dsm_interrupt(signal);
}

__override void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
				       int *ki_div, int *kd, int *kd_div)
{
	*kp = 1;
	*kp_div = 20;
	*ki = 1;
	*ki_div = 250;
	*kd = 0;
	*kd_div = 1;
}

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

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
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

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
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
	},

	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_SUB_USB_C1,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},
};

static int ps8743_tune_mux_c0(const struct usb_mux *me);
static int ps8743_tune_mux_c1(const struct usb_mux *me);

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
				.driver = &ps8743_usb_mux_driver,
				.board_init = &ps8743_tune_mux_c0,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_SUB_USB_C1,
				.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
				.driver = &ps8743_usb_mux_driver,
				.board_init = &ps8743_tune_mux_c1,
			},
	}
};
/* USB Mux C0 : board_init of PS8743 */
static int ps8743_tune_mux_c0(const struct usb_mux *me)
{
	ps8743_tune_usb_eq(me, PS8743_USB_EQ_TX_3_6_DB,
			   PS8743_USB_EQ_RX_16_0_DB);

	return EC_SUCCESS;
}
/* USB Mux C1 : board_init of PS8743 */
static int ps8743_tune_mux_c1(const struct usb_mux *me)
{
	ps8743_tune_usb_eq(me, PS8743_USB_EQ_TX_3_6_DB,
			   PS8743_USB_EQ_RX_16_0_DB);

	ps8743_write(me, PS8743_REG_USB_SWING, PS8743_LFPS_SWG_TD);
	ps8743_write(me, PS8743_REG_DP_SETTING, PS8743_DP_SWG_ADJ_P15P);

	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2 detector IC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			/* The TCPCI Rev 1.0 spec says to ignore bits 14:12. */
			if (!(tcpc_config[0].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (board_get_usb_pd_port_count() > 1 &&
	    !gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			/* TCPCI spec Rev 1.0 says to ignore bits 14:12. */
			if (!(tcpc_config[1].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}
static const struct ec_response_keybd_config keybd1 = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad, has screenlock key */
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	/*
	 * Future boards should use fw_config if needed.
	 */

	return &keybd1;
}

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif
/**
 * Enable panel power detection
 */
static void panel_power_detect_init(void)
{
	gpio_enable_interrupt(GPIO_EN_PP1800_PANEL_S0);
}
DECLARE_HOOK(HOOK_INIT, panel_power_detect_init, HOOK_PRIO_DEFAULT);

/**
 * Handle VPN / VSN for mipi display.
 */
static void panel_power_change_deferred(void)
{
	int signal = gpio_get_level(GPIO_EN_PP1800_PANEL_S0);

	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
	}
	if (board_id < 4) {
		gpio_set_level(GPIO_EN_LCD_ENP, signal);
		crec_msleep(1);
		gpio_set_level(GPIO_EN_LCD_ENN, signal);
	} else if (signal != 0) {
		i2c_write8(I2C_PORT_LCD, I2C_ADDR_ISL98607_FLAGS,
			   ISL98607_REG_VBST_OUT, ISL98607_VBST_OUT_5P65);
		i2c_write8(I2C_PORT_LCD, I2C_ADDR_ISL98607_FLAGS,
			   ISL98607_REG_VN_OUT, ISL98607_VN_OUT_5P5);
		i2c_write8(I2C_PORT_LCD, I2C_ADDR_ISL98607_FLAGS,
			   ISL98607_REG_VP_OUT, ISL98607_VP_OUT_5P5);
	}
	gpio_set_level(GPIO_TSP_TA, signal & extpower_is_present());
}
DECLARE_DEFERRED(panel_power_change_deferred);

void panel_power_change_interrupt(enum gpio_signal signal)
{
	/* Reset lid debounce time */
	hook_call_deferred(&panel_power_change_deferred_data, 1 * MSEC);
}

/*
 * Detect LCD reset & control LCD DCDC power
 */
static void lcd_reset_detect_init(void)
{
	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
	}

	if (board_id < 4)
		return;
	gpio_enable_interrupt(GPIO_DDI0_DDC_SCL);
}
DECLARE_HOOK(HOOK_INIT, lcd_reset_detect_init, HOOK_PRIO_DEFAULT);
/*
 * Handle VSP / VSN for mipi display when lcd turns off
 */
static void lcd_reset_change_deferred(void)
{
	int signal = gpio_get_level(GPIO_DDI0_DDC_SCL);

	if (signal != 0)
		return;

	signal = gpio_get_level(GPIO_EN_PP1800_PANEL_S0);

	if (signal == 0)
		return;

	i2c_write8(I2C_PORT_LCD, I2C_ADDR_ISL98607_FLAGS, ISL98607_REG_ENABLE,
		   ISL97607_VP_VN_VBST_DIS);
}
DECLARE_DEFERRED(lcd_reset_change_deferred);
void lcd_reset_change_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&lcd_reset_change_deferred_data, 45 * MSEC);
}

/**
 * Handle TSP_TA according to AC status
 */
static void handle_tsp_ta(void)
{
	int signal = gpio_get_level(GPIO_EN_PP1800_PANEL_S0);

	gpio_set_level(GPIO_TSP_TA, signal & extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, handle_tsp_ta, HOOK_PRIO_DEFAULT);

/******************************************************************************/
/* USB-A charging control */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_VBUS,
};

/*
 * Change LED Driver Current
 * LED driver current must be written when EN_BL_OD goes from Low to High.
 */
static int backup_enable_backlight = -1;
void backlit_gpio_tick(void)
{
	int signal = gpio_get_level(GPIO_ENABLE_BACKLIGHT);

	if (backup_enable_backlight == signal)
		return;

	backup_enable_backlight = signal;
	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
	}

	if (board_id >= 4 && signal == 1)
		i2c_write16(I2C_PORT_LCD, I2C_ADDR_MP3372_FLAGS,
			    MP3372_REG_ISET_CHEN, MP3372_ISET_15P3_CHEN_ALL);
}
DECLARE_HOOK(HOOK_TICK, backlit_gpio_tick, HOOK_PRIO_DEFAULT);
