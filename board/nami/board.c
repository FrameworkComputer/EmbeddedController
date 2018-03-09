/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Poppy board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_ramp.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/pmic_tps650x30.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/accel_bma2x2.h"
#include "driver/als_opt3001.h"
#include "driver/baro_bmp280.h"
#include "driver/led/lm3509.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/temp_sensor/f75303.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
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
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "espi.h"
#include "fan.h"
#include "fan_chip.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
	    !gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;
	else if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
		 !gpio_get_level(GPIO_USB_C1_PD_RST_L))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

/* Set PD discharge whenever VBUS detection is high (i.e. below threshold). */
static void vbus_discharge_handler(void)
{
	pd_set_vbus_discharge(0, gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));
	//pd_set_vbus_discharge(1, gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L));
}
DECLARE_DEFERRED(vbus_discharge_handler);

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
	hook_call_deferred(&vbus_discharge_handler_data, 0);
}

#if 0
void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
	hook_call_deferred(&vbus_discharge_handler_data, 0);
}
#endif

void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	{GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED"},
#endif
	{VW_SLP_S3_L,		POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{VW_SLP_S4_L,		POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,	POWER_SIGNAL_ACTIVE_HIGH, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD,	POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,	POWER_SIGNAL_ACTIVE_HIGH, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing (10x voltage divider). PPVAR_BOOSTIN_SENSE */
	[ADC_VBUS] = {"VBUS", NPCX_ADC_CH2, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {"AMON_BMON", NPCX_ADC_CH1, ADC_MAX_VOLT*1000/18,
			   ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2800,
	.rpm_start = 3000,
	.rpm_max = 6000,
};

struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {NPCX_MFT_MODULE_2, TCKC_LFCLK, PWM_CH_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[]  = {
	{"tcpc0",     NPCX_I2C_PORT0_0, 400, GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"tcpc1",     NPCX_I2C_PORT0_1, 400, GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"battery",   NPCX_I2C_PORT1,   400, GPIO_I2C1_SCL,   GPIO_I2C1_SDA}, /* dnojiri:verify */
	{"charger",   NPCX_I2C_PORT2,   100, GPIO_I2C2_SCL,   GPIO_I2C2_SDA},
	{"pmic",      NPCX_I2C_PORT2,   400, GPIO_I2C2_SCL,   GPIO_I2C2_SDA},
	{"accelgyro", NPCX_I2C_PORT3,   400, GPIO_I2C3_SCL,   GPIO_I2C3_SDA},
	/* dnojiri: Add KB backlight, ALS, G-sensor, Thermal sensor, BC1.2 Detectors. */
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_0, 0x16, &ps8xxx_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
	//{NPCX_I2C_PORT0_1, 0x16, &ps8xxx_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
	/*
	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
	*/
};

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_0,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	msleep(1);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
}

void board_tcpc_init(void)
{
	int port;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image()) {
		board_reset_pd_mcu();
	}

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

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

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

/*
 * F75303_Remote1 is near CPU, and F75303_Remote2 is near 5V power IC.
 */
const struct temp_sensor_t temp_sensors[] = {
	{"F75303_Remote1", TEMP_SENSOR_TYPE_CPU, f75303_get_val,
		F75303_IDX_REMOTE1, 4},
	{"F75303_Remote2", TEMP_SENSOR_TYPE_BOARD, f75303_get_val,
		F75303_IDX_REMOTE2, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor. All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, <on>
	 * {Twarn, Thigh, X    }, <off>
	 * fan_off, fan_max
	 */
	{{C_TO_K(80), C_TO_K(85), C_TO_K(88)},
	{C_TO_K(75), C_TO_K(80), C_TO_K(83)},
	C_TO_K(40), C_TO_K(80)},	/* TEMP_SENSOR_I2C_F75303_REMOTE1*/
	{{C_TO_K(75), C_TO_K(80), C_TO_K(83)},
	{C_TO_K(70), C_TO_K(75), C_TO_K(78)},
	C_TO_K(35), C_TO_K(75)},	/* TEMP_SENSOR_I2C_F75303_REMOTE2*/
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

#define I2C_PMIC_READ(reg, data) \
		i2c_read8(I2C_PORT_PMIC, TPS650X30_I2C_ADDR1, (reg), (data))
#define I2C_PMIC_WRITE(reg, data) \
		i2c_write8(I2C_PORT_PMIC, TPS650X30_I2C_ADDR1, (reg), (data))

static void board_pmic_init(void)
{
	int err;
	int error_count = 0;
	static uint8_t pmic_initialized = 0;

	if (pmic_initialized)
		return;

	/* Read vendor ID */
	while (1) {
		int data;
		err = I2C_PMIC_READ(TPS650X30_REG_VENDORID, &data);
		if (!err && data == TPS650X30_VENDOR_ID)
			break;
		else if (error_count > 5)
			goto pmic_error;
		error_count++;
	}

	/*
	 * VCCIOCNT register setting
	 * [6] : CSDECAYEN
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_VCCIOCNT, 0x4A);
	if (err)
		goto pmic_error;

	/*
	 * VRMODECTRL:
	 * [4] : VCCIOLPM clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_VRMODECTRL, 0x2F);
	if (err)
		goto pmic_error;

	/*
	 * PGMASK1 : Exclude VCCIO from Power Good Tree
	 * [7] : MVCCIOPG clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PGMASK1, 0x80);
	if (err)
		goto pmic_error;

	/*
	 * PWFAULT_MASK1 Register settings
	 * [7] : 1b V4 Power Fault Masked
	 * [4] : 1b V7 Power Fault Masked
	 * [2] : 1b V9 Power Fault Masked
	 * [0] : 1b V13 Power Fault Masked
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PWFAULT_MASK1, 0x95);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 4 register configuration
	 * [7:6] : 00b Reserved
	 * [5:4] : 01b V3.3S discharge resistance (V6S), 100 Ohm
	 * [3:2] : 01b V18S discharge resistance (V8S), 100 Ohm
	 * [1:0] : 01b V100S discharge resistance (V11S), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT4, 0x15);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 3 register configuration
	 * [7:6] : 01b V1.8U_2.5U discharge resistance (V9), 100 Ohm
	 * [5:4] : 01b V1.2U discharge resistance (V10), 100 Ohm
	 * [3:2] : 01b V100A discharge resistance (V11), 100 Ohm
	 * [1:0] : 01b V085A discharge resistance (V12), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT3, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 2 register configuration
	 * [7:6] : 01b V5ADS3 discharge resistance (V5), 100 Ohm
	 * [5:4] : 01b V33A_DSW discharge resistance (V6), 100 Ohm
	 * [3:2] : 01b V33PCH discharge resistance (V7), 100 Ohm
	 * [1:0] : 01b V18A discharge resistance (V8), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT2, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 1 register configuration
	 * [7:2] : 00b Reserved
	 * [1:0] : 01b VCCIO discharge resistance (V4), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT1, 0x01);
	if (err)
		goto pmic_error;

	/*
	 * Increase Voltage
	 *  [7:0] : 0x2a default
	 *  [5:4] : 10b default
	 *  [5:4] : 01b 5.1V (0x1a)
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_V5ADS3CNT, 0x1a);
	if (err)
		goto pmic_error;

	/*
	 * PBCONFIG Register configuration
	 *   [7] :      1b Power button debounce, 0ms (no debounce)
	 *   [6] :      0b Power button reset timer logic, no action (default)
	 * [5:0] : 011111b Force an Emergency reset time, 31s (default)
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PBCONFIG, 0x9F);
	if (err)
		goto pmic_error;

	CPRINTS("PMIC init done");
	pmic_initialized = 1;
	return;

pmic_error:
	CPRINTS("PMIC init failed: %d", err);
}

static void chipset_pre_init(void)
{
	board_pmic_init();
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	uint32_t version;

	if (cbi_get_board_version(&version) == EC_SUCCESS)
		CPRINTS("Board Version: 0x%04x", version);

	/*
	 * This enables pull-down on F_DIO1 (SPI MISO), and F_DIO0 (SPI MOSI),
	 * whenever the EC is not doing SPI flash transactions. This avoids
	 * floating SPI buffer input (MISO), which causes power leakage (see
	 * b/64797021).
	 */
	NPCX_PUPD_EN1 |= (1 << NPCX_DEVPU1_F_SPI_PUD_EN);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACPRESENT, extpower_is_present());

	/* Enable sensors power supply */
	/* dnojiri: how do we enable it? */

	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	//gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable Gyro interrupt for BMI160 */
	gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACPRESENT, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Set active charge port -- only one port can be active at a time. */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are sourcing VBUS on the port */
	/* dnojiri: revisit */
	int is_source = gpio_get_level(charge_port == 0 ?
			GPIO_USB_C0_5V_EN : GPIO_USB_C1_5V_EN);

	if (is_real_port && is_source) {
		CPRINTF("No charging on source port p%d is ", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		/* dnojiri: revisit. there is always this assumption that
		 * battery is present. If not, this may cause brownout. */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_L :
					     GPIO_USB_C1_CHARGE_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L, 0);
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/*
	 * Limit the input current to 96% negotiated limit,
	 * to account for the charger chip margin.
	 */
	charge_ma = charge_ma * 96 / 100;
	charge_set_input_current_limit(
			MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT),
			charge_mv);
}

void board_hibernate(void)
{
	CPRINTS("Triggering PMIC shutdown.");
	uart_flush_output();
	gpio_set_level(GPIO_EC_HIBERNATE, 1);
	while (1)
		;
}

const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_RED]   = { 3, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_GREEN] = { 5, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_FAN] = {4, PWM_CONFIG_OPEN_DRAIN, 25000},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

static struct bmi160_drv_data_t g_bmi160_data;

/* BMA255 private data */
static struct bma2x2_accel_data g_bma255_data;

static struct opt3001_drv_data_t g_opt3001_data = {
	.scale = 1,
	.uscale = 0,
	.offset = 0,
};
/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
    { FLOAT_TO_FP(-1), 0, 0},
    { 0,  FLOAT_TO_FP(1), 0},
    { 0, 0, FLOAT_TO_FP(-1)}
};

const matrix_3x3_t lid_standard_ref = {
    { FLOAT_TO_FP(-1), 0, 0},
    { 0, FLOAT_TO_FP(-1), 0},
    { 0,  0, FLOAT_TO_FP(1)}
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
		.addr = BMA2x2_I2C_ADDR1,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support tablet mode */
		.config = {
			/* AP: by default use EC settings */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
			/* Sensor off in S5 */
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
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
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI160_ACCEL_MIN_FREQ,
		.max_frequency = BMI160_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support tablet mode  */
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
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
			/* Sensor off in S5 */
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0
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
		.addr = BMI160_ADDR0,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI160_GYRO_MIN_FREQ,
		.max_frequency = BMI160_GYRO_MAX_FREQ,
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
	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_OPT3001,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &opt3001_drv,
		.drv_data = &g_opt3001_data,
		.port = I2C_PORT_ALS,
		.addr = OPT3001_I2C_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1; uscale = 0 */
		.min_frequency = OPT3001_LIGHT_MIN_FREQ,
		.max_frequency = OPT3001_LIGHT_MAX_FREQ,
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
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

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

/* Enable or disable input devices, based on chipset state and tablet mode */
#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	/* If the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard and touchpad. */
	if (tablet_get_mode() || chipset_in_state(CHIPSET_STATE_ANY_OFF))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 0);
	if (lid_is_open())
		lm3509_poweron();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 1);
	lm3509_poweroff();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Control keyboard backlight when Lid status change */
static void lm3509_kblight_lid_change(void)
{
	if (lid_is_open())
		lm3509_poweron();
	else
		lm3509_poweroff();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lm3509_kblight_lid_change, HOOK_PRIO_DEFAULT);
