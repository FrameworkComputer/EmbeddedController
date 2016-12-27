/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Snappy board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/als_opt3001.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/baro_bmp280.h"
#include "driver/charger/bd9995x.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8751.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define IN_ALL_SYS_PG	POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_PGOOD_PP3300	POWER_SIGNAL_MASK(X86_PGOOD_PP3300)
#define IN_PGOOD_PP5000	POWER_SIGNAL_MASK(X86_PGOOD_PP5000)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;

	if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void anx74xx_cable_det_handler(void)
{
	/* confirm if cable_det is asserted */
	if (!gpio_get_level(GPIO_USB_C0_CABLE_DET) ||
		gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;

	task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET, 0);
}
DECLARE_DEFERRED(anx74xx_cable_det_handler);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, anx74xx_cable_det_handler, HOOK_PRIO_LAST);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2ms */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}
#endif

/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 * Use DECLARE_DEFERRED to generate enable_input_devices_data.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

#define LID_DEBOUNCE_US    (30 * MSEC)  /* Debounce time for lid switch */
void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, LID_DEBOUNCE_US);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,     1, "RSMRST_L"},
	{GPIO_PCH_SLP_S0_L,       1, "PMU_SLP_S0_N"},
	{GPIO_PCH_SLP_S3_L,       1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,       1, "SLP_S4_DEASSERTED"},
	{GPIO_SUSPWRNACK,         1, "SUSPWRNACK_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,      1, "ALL_SYS_PGOOD"},
	{GPIO_PP3300_PG,          1, "PP3300_PG"},
	{GPIO_PP5000_PG,          1, "PP5000_PG"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vfs = Vref = 2.816V, 10-bit unsigned reading */
	[ADC_TEMP_SENSOR_CHARGER] = {
		"CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
	[ADC_TEMP_SENSOR_AMB] = {
		"AMBIENT", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
	[ADC_BOARD_ID] = {
		"BRD_ID", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"tcpc0",     NPCX_I2C_PORT0_0, 400,
		GPIO_EC_I2C_USB_C0_PD_SCL, GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",     NPCX_I2C_PORT0_1, 400,
		GPIO_EC_I2C_USB_C1_PD_SCL, GPIO_EC_I2C_USB_C1_PD_SDA},
	{"accelgyro", I2C_PORT_GYRO,   400,
		GPIO_EC_I2C_GYRO_SCL,      GPIO_EC_I2C_GYRO_SDA},
	{"sensors",   NPCX_I2C_PORT2,   400,
		GPIO_EC_I2C_SENSOR_SCL,    GPIO_EC_I2C_SENSOR_SDA},
	{"batt",      NPCX_I2C_PORT3,   100,
		GPIO_EC_I2C_POWER_SCL,     GPIO_EC_I2C_POWER_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef CONFIG_CMD_I2C_STRESS_TEST
struct i2c_stress_test i2c_stress_tests[] = {
/* NPCX_I2C_PORT0_0 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
	{
		.port = NPCX_I2C_PORT0_0,
		.addr = 0x50,
		.i2c_test = &anx74xx_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT0_1 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
	{
		.port = NPCX_I2C_PORT0_1,
		.addr = 0x16,
		.i2c_test = &ps8751_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT1 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
	{
		.port = I2C_PORT_GYRO,
		.addr = BMI160_ADDR0,
		.i2c_test = &bmi160_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT2 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
	{
		.port = I2C_PORT_BARO,
		.addr = BMP280_I2C_ADDRESS1,
		.i2c_test = &bmp280_i2c_stress_test_dev,
	},
	{
		.port = I2C_PORT_LID_ACCEL,
		.addr = KX022_ADDR1,
		.i2c_test = &kionix_i2c_stress_test_dev,
	},
#endif
#ifdef CONFIG_CMD_I2C_STRESS_TEST_ALS
	{
		.i2c_test = &opt3001_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT3 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_BATTERY
	{
		.i2c_test = &battery_i2c_stress_test_dev,
	},
#endif
#ifdef CONFIG_CMD_I2C_STRESS_TEST_CHARGER
	{
		.i2c_test = &bd9995x_i2c_stress_test_dev,
	},
#endif
};
const int i2c_test_dev_used = ARRAY_SIZE(i2c_stress_tests);
#endif /* CONFIG_CMD_I2C_STRESS_TEST */

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_0, 0x50, &anx74xx_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
	{NPCX_I2C_PORT0_1, 0x16, &tcpci_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,	/* don't care / unused */
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8751_tcpc_update_hpd_status,
	}
};

/* called from anx74xx_set_power_mode() */
void board_set_tcpc_power_mode(int port, int mode)
{
	if (port == 0) {
		gpio_set_level(GPIO_USB_C0_PD_RST_L, mode);
		msleep(mode ? 10 : 1);
		gpio_set_level(GPIO_EN_USB_TCPC_PWR, mode);
	}
}

/**
 * Reset PD MCU -- currently only called from handle_pending_reboot() in
 * common/power.c just before hard resetting the system. This logic is likely
 * not needed as the PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* Assert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);

	/* Assert reset to TCPC0 */
	board_set_tcpc_power_mode(0, 0);

	/* Deassert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);

	/* TCPC0 requires 10ms reset/power down assertion */
	msleep(10);

	/* Deassert reset to TCPC0 */
	board_set_tcpc_power_mode(0, 1);
}

#ifdef CONFIG_USB_PD_TCPC_FW_VERSION
void board_print_tcpc_fw_version(int port)
{
	int rv;
	int version;

	if (port)
		rv = ps8751_tcpc_get_fw_version(port, &version);
	else
		rv = anx74xx_tcpc_get_fw_version(port, &version);

	if (!rv)
		CPRINTS("TCPC p%d FW VER: 0x%x", port, version);
}
#endif

void board_tcpc_init(void)
{
	int port, reg;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/*
	 * Force PS8751 A2 to wake from low power mode.
	 * If PS8751 remains in low power mode after sysjump,
	 * TCPM_INIT will fail due to not able to access PS8751.
	 *
	 * NOTE: PS8751 A3 will wake on any I2C access.
	 */
	i2c_read8(NPCX_I2C_PORT0_1, 0x10, 0xA0, &reg);

	/* Enable TCPC0 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable TCPC1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
#endif
	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C+1);

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
	int mv = adc_read_channel(NPCX_ADC_CH0);

	if (mv < 0)
		return -1;

	*temp_ptr = thermistor_linear_interpolate(mv, &charger_thermistor_info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return 0;
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
	int mv = adc_read_channel(NPCX_ADC_CH1);

	if (mv < 0)
		return -1;

	*temp_ptr = thermistor_linear_interpolate(mv, &amb_thermistor_info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return 0;
}


const struct temp_sensor_t temp_sensors[] = {
	/* FIXME(dhendrix): tweak action_delay_sec */
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0, 1},
	{"Ambient", TEMP_SENSOR_TYPE_BOARD, board_get_ambient_temp, 0, 5},
	{"Charger", TEMP_SENSOR_TYPE_BOARD, board_get_charger_temp, 1, 1},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	/* FIXME(dhendrix): verify attenuation_factor */
	{"TI", opt3001_init, opt3001_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_EC_VOLDN_BTN_ODL,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_EC_VOLUP_BTN_ODL,
	 30 * MSEC, 0},
};

/* Called by APL power state machine when transitioning from G3 to S5 */
static void chipset_pre_init(void)
{
	/*
	 * No need to re-init PMIC since settings are sticky across sysjump.
	 * However, be sure to check that PMIC is already enabled. If it is
	 * then there's no need to re-sequence the PMIC.
	 */
	if (system_jumped_to_this_image() && gpio_get_level(GPIO_PMIC_EN))
		return;

	/* Enable PP5000 before PP3300 due to NFC: chrome-os-partner:50807 */
	gpio_set_level(GPIO_EN_PP5000, 1);
	while (!gpio_get_level(GPIO_PP5000_PG))
		;

	/*
	 * To prevent SLP glitches, PMIC_EN (V5A_EN) should be enabled
	 * at the same time as PP3300 (chrome-os-partner:51323).
	 */
	/* Enable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);

static void board_set_tablet_mode(void)
{
	tablet_set_mode(!gpio_get_level(GPIO_TABLET_MODE_L));
}

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality.
	 */
	board_set_tablet_mode();

	gpio_enable_interrupt(GPIO_TABLET_MODE_L);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_L);

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
}
/* PP3300 needs to be enabled before TCPC init hooks */
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_FIRST);

int pd_snk_is_vbus_provided(int port)
{
	enum bd9995x_charge_port bd9995x_port;

	switch (port) {
	case 0:
	case 1:
		bd9995x_port = bd9995x_pd_port_to_chg_port(port);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	return bd9995x_is_vbus_provided(bd9995x_port);
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	enum bd9995x_charge_port bd9995x_port;
	int bd9995x_port_select = 1;
	static int initialized;

	/*
	 * Reject charge port disable if our battery is critical and we
	 * have yet to initialize a charge port - continue to charge using
	 * charger ROM / POR settings.
	 */
	if (!initialized &&
	    charge_port == CHARGE_PORT_NONE &&
	    charge_get_percent() < 2)
		return -1;

	switch (charge_port) {
	case 0:
	case 1:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		bd9995x_port = bd9995x_pd_port_to_chg_port(charge_port);
		break;
	case CHARGE_PORT_NONE:
		bd9995x_port_select = 0;
		bd9995x_port = BD9995X_CHARGE_PORT_BOTH;

		/*
		 * To avoid inrush current from the external charger, enable
		 * discharge on AC till the new charger is detected and
		 * charge detect delay has passed.
		 */
		if (charge_get_percent() > 2)
			charger_discharge_on_ac(1);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	CPRINTS("New chg p%d", charge_port);
	initialized = 1;

	return bd9995x_select_input_port(bd9995x_port, bd9995x_port_select);
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* Enable charging trigger by BC1.2 detection */
	int bc12_enable = (supplier == CHARGE_SUPPLIER_BC12_CDP ||
			   supplier == CHARGE_SUPPLIER_BC12_DCP ||
			   supplier == CHARGE_SUPPLIER_BC12_SDP ||
			   supplier == CHARGE_SUPPLIER_OTHER);

	if (bd9995x_bc12_enable_charging(port, bc12_enable))
		return;

	charge_set_input_current_limit(MAX(charge_ma,
				   CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
		&& system_is_locked())
		return 0;
	else
		return (supplier == CHARGE_SUPPLIER_BC12_DCP ||
			supplier == CHARGE_SUPPLIER_BC12_SDP ||
			supplier == CHARGE_SUPPLIER_BC12_CDP ||
			supplier == CHARGE_SUPPLIER_OTHER);
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	return bd9995x_get_bc12_ilim(supplier);
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	int chg_perc = charge_get_percent();

	return chg_perc > 2 && chg_perc < 95;
}

/**
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(enum chg_ramp_vbus_state ramp_state)
{
	return charger_get_vbus_level() < BD9995X_BC12_MIN_VOLTAGE;
}

static void enable_input_devices(void)
{
	/* We need to turn on tablet mode for motion sense */
	board_set_tablet_mode();

	/*
	 * Then, we disable peripherals only when the lid reaches 360 position.
	 * (It's probably already disabled by motion_sense_task.)
	 * We deliberately do not enable peripherals when the lid is leaving
	 * 360 position. Instead, we let motion_sense_task enable it once it
	 * reaches laptop zone (180 or less).
	 */
	if (tablet_get_mode())
		lid_angle_peripheral_enable(0);
}

/* Enable or disable input devices, based on chipset state and tablet mode */
#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard and touchpad.
	 */
	if (tablet_get_mode() || chipset_in_state(CHIPSET_STATE_ANY_OFF))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, !enable);
}
#endif

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	/* Enable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 1);

	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 0);

	hook_call_deferred(&enable_input_devices_data, 0);
	/*
	 * FIXME(dhendrix): Drive USB_PD_RST_ODL low to prevent
	 * leakage? (see comment in schematic)
	 */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/*
 * FIXME(dhendrix): Add CHIPSET_RESUME and CHIPSET_SUSPEND
 * hooks to enable/disable sensors?
 */

/*
 * FIXME(dhendrix): Weak symbol hack until we can get a better solution for
 * both Amenia and Snappy.
 */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/*Disable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP3300_PG))
		;

	/*Disable 5V rail */
	gpio_set_level(GPIO_EN_PP5000, 0);
	while (gpio_get_level(GPIO_PP5000_PG))
		;
}

void board_hibernate_late(void)
{
	int i;
	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs in hibernate */
		{GPIO_POWER_LED_WHITE, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_WHITE, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_AMBER, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_LID_OPEN, GPIO_INT_RISING | GPIO_PULL_DOWN},

		/*
		 * BD99956 handles charge input automatically. We'll disable
		 * charge output in hibernate. Charger will assert ACOK_OD
		 * when VBUS or VCC are plugged in.
		 */
		{GPIO_USB_C0_5V_EN,       GPIO_INPUT  | GPIO_PULL_DOWN},
		{GPIO_USB_C1_5V_EN,       GPIO_INPUT  | GPIO_PULL_DOWN},
	};

	/* Change GPIOs' state in hibernate for better power consumption */
	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);

	gpio_config_module(MODULE_KEYBOARD_SCAN, 0);

	/*
	 * Calling gpio_config_module sets disabled alternate function pins to
	 * GPIO_INPUT.  But to prevent keypresses causing leakage currents
	 * while hibernating we want to enable GPIO_PULL_UP as well.
	 */
	gpio_set_flags_by_mask(0x2, 0x03, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x1, 0x7F, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x0, 0xE0, GPIO_INPUT | GPIO_PULL_UP);
	/* KBD_KSO2 needs to have a pull-down enabled instead of pull-up */
	gpio_set_flags_by_mask(0x1, 0x80, GPIO_INPUT | GPIO_PULL_DOWN);
}

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

const matrix_3x3_t lid_standard_ref = {
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

struct kionix_accel_data g_kx022_data;
struct bmi160_drv_data_t g_bmi160_data;
struct bmp280_drv_data_t bmp280_drv_data;

/* FIXME(dhendrix): Copied from Amenia, probably need to tweak for Snappy */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KX022,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kx022_data,
	 .port = I2C_PORT_LID_ACCEL,
	 .addr = KX022_ADDR1,
	 .rot_standard_ref = &lid_standard_ref, /* Identity matrix. */
	 .default_range = 2, /* g, enough for laptop. */
	 .config = {
		/* AP: by default use EC settings */
		[SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* unused */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		},
		[SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		},
	 },
	},

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 2,  /* g, enough for laptop. */
	 .config = {
		 /* AP: by default use EC settings */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		 },
	 },
	},

	[BASE_MAG] = {
	 .name = "Base Mag",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .default_range = 1 << 11, /* 16LSB / uT, fixed */
	 .rot_standard_ref = NULL,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			 .odr = 0,
			 .ec_rate = 0,
		 },
	 },
	},

	[BASE_BARO] = {
	 .name = "Base Baro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMP280,
	 .type = MOTIONSENSE_TYPE_BARO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmp280_drv,
	 .drv_data = &bmp280_drv_data,
	 .port = I2C_PORT_BARO,
	 .addr = BMP280_I2C_ADDRESS1,
	 .default_range = 1 << 18, /*  1bit = 4 Pa, 16bit ~= 2600 hPa */
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		 },
	 },
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void board_hibernate(void)
{
	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_do_shutdown();

	/* Added delay to allow AP to settle down */
	msleep(100);

	/* Enable both the VBUS & VCC ports before entering PG3 */
	bd9995x_select_input_port(BD9995X_CHARGE_PORT_BOTH, 1);

	/* Turn BGATE OFF for saving the power */
	bd9995x_set_power_save_mode(BD9995X_PWR_SAVE_MAX);
}

struct {
	enum snappy_board_version version;
	int thresh_mv;
} const snappy_board_versions[] = {
	/* Vin = 3.3V, R1 = 46.4K, R2 values listed below */
	{ BOARD_VERSION_1, 328 * 1.03 },  /* 5.11 Kohm */
	{ BOARD_VERSION_2, 670 * 1.03 },  /* 11.8 Kohm */
	{ BOARD_VERSION_3, 1012 * 1.03 }, /* 20.5 Kohm */
	{ BOARD_VERSION_4, 1357 * 1.03 }, /* 32.4 Kohm */
	{ BOARD_VERSION_5, 1690 * 1.03 }, /* 48.7 Kohm */
	{ BOARD_VERSION_6, 2020 * 1.03 }, /* 73.2 Kohm */
	{ BOARD_VERSION_7, 2352 * 1.03 }, /* 115 Kohm */
	{ BOARD_VERSION_8, 2802 * 1.03 }, /* 261 Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(snappy_board_versions) == BOARD_VERSION_COUNT);

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv, i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	/* FIXME(dhendrix): enable ADC */
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_ODR_HIGH);
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 0);
	/* Wait to allow cap charge */
	msleep(1);
	mv = adc_read_channel(ADC_BOARD_ID);
	/* FIXME(dhendrix): disable ADC */
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 1);
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_INPUT);

	if (mv == ADC_READ_ERROR) {
		version = BOARD_VERSION_UNKNOWN;
		return version;
	}

	for (i = 0; i < BOARD_VERSION_COUNT; i++) {
		if (mv < snappy_board_versions[i].thresh_mv) {
			version = snappy_board_versions[i].version;
			break;
		}
	}

	CPRINTS("Board version: %d\n", version);
	return version;
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * We have total 21 pins for keyboard connecter, {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {1, 4}, {1, 3}, {1, 6}, {-1, -1},
		{3, 1}, {2, 0}, {1, 5}, {2, 6}, {-1, -1},
		{2, 1}, {2, 4}, {2, 5}, {1, 2}, {2, 3},
		{2, 2}, {3, 0},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);
#endif
