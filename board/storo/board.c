/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Storo configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_kionix.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi260.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm42607.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/tusb544.h"
#include "driver/tcpm/raa489000.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprints(CC_SYSTEM, format, ##args)

#define INT_RECHECK_US 5000

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
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
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}

static void c0_ccsbu_ovp_interrupt(enum gpio_signal s)
{
	cprints(CC_USBPD, "C0: CC OVP, SBU OVP, or thermal event");
	pd_handle_cc_overvoltage(0);
}

static void pen_detect_interrupt(enum gpio_signal s)
{
	int pen_detect = !gpio_get_level(GPIO_PEN_DET_ODL);

	gpio_set_level(GPIO_EN_PP3300_PEN, pen_detect);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH15 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* BC 1.2 chips */
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

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	[CHARGER_PRIMARY] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
	[CHARGER_SECONDARY] = {
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* TCPCs */
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

/* USB Retimer */
enum tusb544_conf { USB_DP = 0, USB_DP_INV, USB, USB_INV, DP, DP_INV };

/*
 * Registers we care about of are all the same between NCS8510 and TUSB544,
 * so we leverage the driver of TUSB544 to control both of them.
 *
 * For EQ settings, these two chips are also almost the same, so we have one
 * set of EQ settings here for both of them as well. When you need to modify
 * the EQ settings, please make sure that both configurations are correct;
 * otherwise you need to separate EQ settings then.
 */
static int board_tusb544_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int reg;
	enum tusb544_conf usb_mode = 0;

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			/* USB with DP */
			usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
					   USB_DP_INV :
					   USB_DP;
		} else {
			/* USB without DP */
			usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
					   USB_INV :
					   USB;
		}
	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* DP without USB */
		usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ? DP_INV :
									DP;
	} else {
		return EC_SUCCESS;
	}

	rv = i2c_read8(me->i2c_port, me->i2c_addr_flags, TUSB544_REG_GENERAL6,
		       &reg);
	if (rv)
		return rv;

	reg |= TUSB544_VOD_DCGAIN_OVERRIDE;
	reg &= ~TUSB544_VOD_DCGAIN_SEL;
	reg |= (TUSB544_VOD_DCGAIN_SETTING_5 << 2);

	rv = i2c_write8(me->i2c_port, me->i2c_addr_flags, TUSB544_REG_GENERAL6,
			reg);
	if (rv)
		return rv;

	/* Write the retimer config byte */
	if (usb_mode == USB_INV) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x15);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_1, 0xff);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_2, 0xff);
	} else if (usb_mode == USB) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x11);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_1, 0xff);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_2, 0xff);
	} else if (usb_mode == USB_DP_INV) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x1F);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_1, 0xff);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_2, 0xff);
	} else if (usb_mode == USB_DP) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x1B);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_1, 0xff);
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_USB3_1_2, 0xff);
	} else if (usb_mode == DP_INV) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x1E);
	} else if (usb_mode == DP) {
		rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
				 TUSB544_REG_GENERAL4, 0x1A);
	}

	rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
			 TUSB544_REG_DISPLAYPORT_1, 0x66);
	rv |= i2c_write8(me->i2c_port, me->i2c_addr_flags,
			 TUSB544_REG_DISPLAYPORT_2, 0x66);
	if (rv)
		return EC_ERROR_UNKNOWN;
	else
		return EC_SUCCESS;
}

/* USB Retimer */
const struct usb_mux_chain usbc1_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = 1,
			.i2c_port = I2C_PORT_SUB_USB_C1,
			.i2c_addr_flags = TUSB544_I2C_ADDR_FLAGS0,
			.driver = &tusb544_drv,
			.board_set = &board_tusb544_set,
		},
};

/* USB Muxes */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_SUB_USB_C1,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
			},
		.next = &usbc1_retimer,
	},
};

void board_hibernate(void)
{
	/*
	 * Put all charger ICs present into low power mode before entering
	 * z-state.
	 */
	raa489000_hibernate(CHARGER_PRIMARY, true);
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(CHARGER_SECONDARY, true);
}

__override void board_ocpc_init(struct ocpc_data *ocpc)
{
	/* There's no provision to measure Isys */
	ocpc->chg_flags[CHARGER_SECONDARY] |= OCPC_NO_ISYS_MEAS_CAP;
}

__override void board_pulse_entering_rw(void)
{
	/*
	 * On the ITE variants, the EC_ENTERING_RW signal was connected to a pin
	 * which is active high by default.  This causes Cr50 to think that the
	 * EC has jumped to its RW image even though this may not be the case.
	 * The pin is changed to GPIO_EC_ENTERING_RW2.
	 */
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 1);
	usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
}

void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal, TCPC C1 reset pin is not
	 * connected to the EC.
	 */
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Motherboard has a GPIO to turn on the 5V regulator, but the sub-board
	 * sets it through the charger GPIO.
	 */
	gpio_set_level(GPIO_EN_PP5000, !!enable);
	gpio_set_level(GPIO_EN_USB_A0_VBUS, !!enable);
	if (isl923x_set_comparator_inversion(1, !!enable))
		CPRINTS("Failed to %sable sub rails!", enable ? "en" : "dis");
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
			if (regval)
				status = PD_STATUS_TCPC_ALERT_0;
		}
	}

	/* Check whether TCPC 1 pulled the shared interrupt line */
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
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
	 * Turn off the other ports sink path FETs, before enabling the
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
	if ((old_port != CHARGE_PORT_NONE) && (old_port != port))
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

__override void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
				       int *ki_div, int *kd, int *kd_div)
{
	*kp = 3;
	*kp_div = 14;

	*ki = 3;
	*ki_div = 500;

	*kd = 4;
	*kd_div = 40;
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
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					      { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static struct accelgyro_saved_data_t g_bma253_data;
static struct bmi_drv_data_t g_bmi160_data;

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
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR2_FLAGS,
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

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static const mat33_fp_t lid_lis2dwl_ref = { { 0, FLOAT_TO_FP(1), 0 },
					    { FLOAT_TO_FP(1), 0, 0 },
					    { 0, 0, FLOAT_TO_FP(-1) } };

/* Lid accel private data */
static struct stprivate_data g_lis2dwl_data;
struct motion_sensor_t lis2dwl_lid_accel = {

		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DWL,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dw12_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2dwl_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
		.rot_standard_ref = &lid_lis2dwl_ref,
		.default_range = 2, /* g */
		.min_frequency = LIS2DW12_ODR_MIN_VAL,
		.max_frequency = LIS2DW12_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
};

static const mat33_fp_t lid_KX022_ref = { { 0, FLOAT_TO_FP(1), 0 },
					  { FLOAT_TO_FP(1), 0, 0 },
					  { 0, 0, FLOAT_TO_FP(-1) } };

static struct kionix_accel_data g_kx022_data;
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
	.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	.rot_standard_ref = &lid_KX022_ref,
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

static struct icm_drv_data_t g_icm42607_data;
const mat33_fp_t based_ref_icm42607 = { { FLOAT_TO_FP(1), 0, 0 },
					{ 0, FLOAT_TO_FP(1), 0 },
					{ 0, 0, FLOAT_TO_FP(1) } };
struct motion_sensor_t icm42607_base_accel = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_ICM42607,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &icm42607_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_icm42607_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
	 .default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	 .rot_standard_ref = &based_ref_icm42607,
	 .min_frequency = ICM42607_ACCEL_MIN_FREQ,
	 .max_frequency = ICM42607_ACCEL_MAX_FREQ,
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

struct motion_sensor_t icm42607_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM42607,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm42607_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm42607_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &based_ref_icm42607,
	.min_frequency = ICM42607_GYRO_MIN_FREQ,
	.max_frequency = ICM42607_GYRO_MAX_FREQ,
};

static struct bmi_drv_data_t g_bmi220_data;
const mat33_fp_t based_ref_bmi220 = { { 0, FLOAT_TO_FP(1), 0 },
				      { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };
struct motion_sensor_t bmi220_base_accel = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI220,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi260_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi220_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	 .default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	 .rot_standard_ref = &based_ref_bmi220,
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
};

struct motion_sensor_t bmi220_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI220,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_bmi220_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &based_ref_bmi220,
	.min_frequency = BMI_GYRO_MIN_FREQ,
	.max_frequency = BMI_GYRO_MAX_FREQ,
};

void board_init(void)
{
	int on;
	uint32_t board_id;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_INT_ODL);

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL))
		hook_call_deferred(&check_c0_line_data, 0);
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL))
		hook_call_deferred(&check_c1_line_data, 0);

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);
	/* Enable Base Accel interrupt */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	/* Enable gpio interrupt for pen detect */
	gpio_enable_interrupt(GPIO_PEN_DET_ODL);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	if (!gpio_get_level(GPIO_PEN_DET_ODL))
		gpio_set_level(GPIO_EN_PP3300_PEN, 1);

	cbi_get_board_version(&board_id);

	if (board_id > 2) {
		if (get_cbi_fw_config_tablet_mode()) {
			if (get_cbi_ssfc_base_sensor() ==
			    SSFC_SENSOR_ICM42607) {
				motion_sensors[BASE_ACCEL] =
					icm42607_base_accel;
				motion_sensors[BASE_GYRO] = icm42607_base_gyro;
				CPRINTF("BASE GYRO is ICM42607");
			} else if (get_cbi_ssfc_base_sensor() ==
				   SSFC_SENSOR_BMI220) {
				motion_sensors[BASE_ACCEL] = bmi220_base_accel;
				motion_sensors[BASE_GYRO] = bmi220_base_gyro;
				CPRINTF("BASE GYRO is BMI220");
			} else {
				CPRINTF("BASE GYRO is BMI160");
			}

			if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_LIS2DWL) {
				motion_sensors[LID_ACCEL] = lis2dwl_lid_accel;
				CPRINTF("LID_ACCEL is LIS2DWL");
			} else if (get_cbi_ssfc_lid_sensor() ==
				   SSFC_SENSOR_KX022) {
				motion_sensors[LID_ACCEL] = kx022_lid_accel;
				CPRINTF("LID_ACCEL is KX022");
			} else {
				CPRINTF("LID_ACCEL is BMA253");
			}
		} else {
			motion_sensor_count = 0;
			gmr_tablet_switch_disable();
			/*
			 * Base accel is not stuffed, don't allow
			 * line to float.
			 */
			gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
				       GPIO_INPUT | GPIO_PULL_DOWN);
		}
	} else {
		if (get_cbi_ssfc_base_sensor() == SSFC_SENSOR_ICM42607) {
			motion_sensors[BASE_ACCEL] = icm42607_base_accel;
			motion_sensors[BASE_GYRO] = icm42607_base_gyro;
			CPRINTF("BASE GYRO is ICM42607");
		} else if (get_cbi_ssfc_base_sensor() == SSFC_SENSOR_BMI220) {
			motion_sensors[BASE_ACCEL] = bmi220_base_accel;
			motion_sensors[BASE_GYRO] = bmi220_base_gyro;
			CPRINTF("BASE GYRO is BMI220");
		} else {
			CPRINTF("BASE GYRO is BMI160");
		}

		if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_LIS2DWL) {
			motion_sensors[LID_ACCEL] = lis2dwl_lid_accel;
			CPRINTF("LID_ACCEL is LIS2DWL");
		} else if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_KX022) {
			motion_sensors[LID_ACCEL] = kx022_lid_accel;
			CPRINTF("LID_ACCEL is KX022");
		} else {
			CPRINTF("LID_ACCEL is BMA253");
		}
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void motion_interrupt(enum gpio_signal signal)
{
	switch (get_cbi_ssfc_base_sensor()) {
	case SSFC_SENSOR_ICM42607:
		icm42607_interrupt(signal);
		break;
	case SSFC_SENSOR_BMI220:
		bmi260_interrupt(signal);
		break;
	case SSFC_SENSOR_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Cpu",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
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

void board_charger_setting(void)
{
	int reg, port;
	int chgnum = charge_get_active_chg_chip();

	if (extpower_is_present()) {
		/* disable SMBUS TIMEOUT */
		if (i2c_read16(chg_chips[chgnum].i2c_port,
			       I2C_ADDR_CHARGER_FLAGS, ISL923X_REG_CONTROL0,
			       &reg) == EC_SUCCESS) {
			reg |= RAA489000_C0_SMBUT_TIMEOUT;
			if (i2c_write16(chg_chips[chgnum].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_CONTROL0, reg))
				CPRINTF("C%d ISL9238_REG_CONTROL0 write fail!",
					chgnum);
		}

		/* enable two level current limit */
		if (i2c_read16(chg_chips[chgnum].i2c_port,
			       I2C_ADDR_CHARGER_FLAGS, ISL923X_REG_CONTROL2,
			       &reg) == EC_SUCCESS) {
			reg |= ISL923X_C2_2LVL_OVERCURRENT;
			if (i2c_write16(chg_chips[chgnum].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_CONTROL2, reg))
				CPRINTF("C%d ISL923X_REG_CONTROL2 write fail!",
					chgnum);
		}

		/* set t2 1ms, t1 20ms */
		if (i2c_read16(chg_chips[chgnum].i2c_port,
			       I2C_ADDR_CHARGER_FLAGS, ISL923X_REG_T1_T2,
			       &reg) == EC_SUCCESS) {
			reg |= (RAA489000_T2_1000 | RAA489000_T1_20000);
			if (i2c_write16(chg_chips[chgnum].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_T1_T2, reg))
				CPRINTF("C%d ISL9238_REG_T1_T2 write fail!",
					chgnum);
		}
	}

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		if (port == chgnum)
			continue;
		/* disable SMBUS TIMEOUT */
		if (i2c_read16(chg_chips[port].i2c_port, I2C_ADDR_CHARGER_FLAGS,
			       ISL923X_REG_CONTROL0, &reg) == EC_SUCCESS) {
			reg &= ~RAA489000_C0_SMBUT_TIMEOUT;
			if (i2c_write16(chg_chips[port].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_CONTROL0, reg))
				CPRINTF("C%d ISL9238_REG_CONTROL0 write fail!",
					port);
		}

		/* disable two level current limit */
		if (i2c_read16(chg_chips[port].i2c_port, I2C_ADDR_CHARGER_FLAGS,
			       ISL923X_REG_CONTROL2, &reg) == EC_SUCCESS) {
			reg &= ~ISL923X_C2_2LVL_OVERCURRENT;
			if (i2c_write16(chg_chips[port].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_CONTROL2, reg))
				CPRINTF("C%d ISL923X_REG_CONTROL2 write fail!",
					port);
		}

		/* set default value t2 10 us, t1 10ms */
		if (i2c_read16(chg_chips[port].i2c_port, I2C_ADDR_CHARGER_FLAGS,
			       ISL923X_REG_T1_T2, &reg) == EC_SUCCESS) {
			reg &= ((RAA489000_T2_10 | RAA489000_T1_10000) &
				0xffff);
			if (i2c_write16(chg_chips[port].i2c_port,
					I2C_ADDR_CHARGER_FLAGS,
					ISL923X_REG_T1_T2, reg))
				CPRINTF("C%d ISL9238_REG_T1_T2 write fail!",
					port);
		}
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, board_charger_setting,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_charger_setting, HOOK_PRIO_DEFAULT);

/* Called when the charge manager has switched to a new port. */
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	int chgnum = charge_get_active_chg_chip();

	charge_set_input_current_limit(charge_ma, charge_mv);

	if (charge_ma > 400)
		charge_ma = 400;

	/* set current limit 2 */
	isl923x_set_level_2_input_current_limit(chgnum, charge_ma);
}
