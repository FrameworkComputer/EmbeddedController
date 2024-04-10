/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nocturne board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_opt3001.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/sn5s330.h"
#include "driver/sync.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/bd99992gw.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "lpc.h"
#include "mkbp_event.h"
#include "motion_sense.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal s)
{
	int port = -1;

	switch (s) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

/*
 * Nocturne shares the TCPC Alert# line with the TI SN5S330's interrupt line.
 * Therefore, we need to also check on that part.
 */
static void usb_c_interrupt(enum gpio_signal s)
{
	int port = (s == GPIO_USB_C0_PD_INT_ODL) ? 0 : 1;

	tcpc_alert_event(s);
	sn5s330_interrupt(port);
}

static void board_connect_c0_sbu_deferred(void)
{
	/*
	 * If CCD_MODE_ODL asserts, it means there's a debug accessory connected
	 * and we should enable the SBU FETs.
	 */
	ppc_set_sbu(0, 1);
}
DECLARE_DEFERRED(board_connect_c0_sbu_deferred);

static void board_connect_c0_sbu(enum gpio_signal s)
{
	hook_call_deferred(&board_connect_c0_sbu_deferred_data, 0);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	[ADC_BASE_ATTACH] = { "BASE ATTACH", NPCX_ADC_CH0, ADC_MAX_VOLT,
			      ADC_READ_MAX + 1, 0 },

	[ADC_BASE_DETACH] = { "BASE DETACH", NPCX_ADC_CH1, ADC_MAX_VOLT,
			      ADC_READ_MAX + 1, 0 },
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_DB0_LED_RED] = { 3, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				 986 },
	[PWM_CH_DB0_LED_GREEN] = { 0, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   986 },
	[PWM_CH_DB0_LED_BLUE] = { 2, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				  986 },
	[PWM_CH_DB1_LED_RED] = { 7, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				 986 },
	[PWM_CH_DB1_LED_GREEN] = { 5, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   986 },
	[PWM_CH_DB1_LED_BLUE] = { 6, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				  986 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "battery",
	  .port = I2C_PORT_BATTERY,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C4_BATTERY_SCL,
	  .sda = GPIO_EC_I2C4_BATTERY_SDA },

	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C0_POWER_SCL,
	  .sda = GPIO_EC_I2C0_POWER_SDA },

	{ .name = "als_gyro",
	  .port = I2C_PORT_ALS_GYRO,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C5_ALS_GYRO_SCL,
	  .sda = GPIO_EC_I2C5_ALS_GYRO_SDA },

	{ .name = "usbc0",
	  .port = I2C_PORT_USB_C0,
	  .kbps = 100,
	  .scl = GPIO_USB_C0_SCL,
	  .sda = GPIO_USB_C0_SDA },

	{ .name = "usbc1",
	  .port = I2C_PORT_USB_C1,
	  .kbps = 100,
	  .scl = GPIO_USB_C1_SCL,
	  .sda = GPIO_USB_C1_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*
 * Motion Sense
 */

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

/* Sensor driver data */
static struct bmi_drv_data_t g_bmi160_data;
static struct opt3001_drv_data_t g_opt3001_data = {
	.scale = 1,
	.uscale = 0,
	.offset = 0,
};

/* Matrix to rotate accel/gyro into standard reference frame. */
const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
				      { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "BMI160 ACC",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ALS_GYRO,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			/* EC setup accel for chrome usage */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},

	[LID_GYRO] = {
		.name = "BMI160 GYRO",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ALS_GYRO,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 1000, /* dps */
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},

	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_OPT3001,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &opt3001_drv,
		.drv_data = &g_opt3001_data,
		.port = I2C_PORT_ALS_GYRO,
		.i2c_spi_addr_flags = OPT3001_I2C_ADDR_FLAGS,
		.rot_standard_ref = NULL,
		/* scale = 43.4513 http://b/111528815#comment14 */
		.default_range = 0x2b11a1,
		.min_frequency = OPT3001_LIGHT_MIN_FREQ,
		.max_frequency = OPT3001_LIGHT_MAX_FREQ,
		.config = {
			/* Run ALS sensor in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
			},
		},
	},

	[VSYNC] = {
		.name = "Camera VSYNC",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_GPIO,
		.type = MOTIONSENSE_TYPE_SYNC,
		.location = MOTIONSENSE_LOC_CAMERA,
		.drv = &sync_drv,
		.default_range = 0,
		.min_frequency = 0,
		.max_frequency = 1,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

static void disable_sensor_irqs(void)
{
	/*
	 * In S5, sensors are unpowered, therefore disable their interrupts on
	 * shutdown.
	 */
	gpio_disable_interrupt(GPIO_ACCELGYRO3_INT_L);
	gpio_disable_interrupt(GPIO_RCAM_VSYNC);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, disable_sensor_irqs, HOOK_PRIO_DEFAULT);

static void enable_sensor_irqs(void)
{
	/*
	 * Re-enable the sensor interrupts when entering S0.
	 */
	gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);
	gpio_enable_interrupt(GPIO_RCAM_VSYNC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_sensor_irqs, HOOK_PRIO_DEFAULT);

struct ppc_config_t ppc_chips[] = {
	{ .i2c_port = I2C_PORT_USB_C0,
	  .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	  .drv = &sn5s330_drv },
	{
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	},

	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	},
};

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

void board_chipset_startup(void)
{
	gpio_set_level(GPIO_EN_5V, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void imvp8_tune_deferred(void)
{
	/* For the IMVP8, reduce the steps during decay from 3 to 1. */
	if (i2c_write16(I2C_PORT_POWER, I2C_ADDR_MP2949_FLAGS, 0xFA, 0x0AC5))
		CPRINTS("Failed to change step decay!");
}
DECLARE_DEFERRED(imvp8_tune_deferred);

void board_chipset_resume(void)
{
	/* Write to the IMVP8 after 250ms. */
	hook_call_deferred(&imvp8_tune_deferred_data, 250 * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_EN_5V, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

int board_get_version(void)
{
	static int board_version = -1;

	if (board_version == -1) {
		board_version = 0;
		/* BRD_ID0 is LSb. */
		if (gpio_get_level(GPIO_EC_BRD_ID0))
			board_version |= 0x1;
		if (gpio_get_level(GPIO_EC_BRD_ID1))
			board_version |= 0x2;
		if (gpio_get_level(GPIO_EC_BRD_ID2))
			board_version |= 0x4;
		if (gpio_get_level(GPIO_EC_BRD_ID3))
			board_version |= 0x8;
	}

	return board_version;
}

void board_hibernate(void)
{
	int p;

	/* Configure PSL pins */
	for (p = 0; p < hibernate_wake_pins_used; p++)
		system_config_psl_mode(hibernate_wake_pins[p]);

	/*
	 * Enter PSL mode.  Note that on Nocturne, simply enabling PSL mode does
	 * not cut the EC's power.  Therefore, we'll need to cut off power via
	 * the ROP PMIC afterwards.
	 */
	system_enter_psl_mode();

	/* Cut off DSW power via the ROP PMIC. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x49, 0x1);

	/* Wait for power to be cut. */
	while (1)
		;
}

static void board_init(void)
{
	/* Enable USB Type-C interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/* Enable sensor IRQs if we're in S0. */
	if (chipset_in_state(CHIPSET_STATE_ON))
		enable_sensor_irqs();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int board_is_i2c_port_powered(int port)
{
	if (port != I2C_PORT_ALS_GYRO)
		return 1;

	/* The sensors are not powered in anything lower than S5. */
	return chipset_in_state(CHIPSET_STATE_ANY_OFF) ? 0 : 1;
}

static void board_lid_change(void)
{
	/* This is done in hardware on old revisions. */
	if (board_get_version() <= 1)
		return;

	if (lid_is_open())
		gpio_set_level(GPIO_UHALL_PWR_EN, 1);
	else
		gpio_set_level(GPIO_UHALL_PWR_EN, 0);
}
DECLARE_HOOK(HOOK_LID_CHANGE, board_lid_change, HOOK_PRIO_DEFAULT);

static void board_pmic_disable_slp_s0_vr_decay(void)
{
	/*
	 * VCCIOCNT:
	 * Bit 6    (0)   - Disable decay of VCCIO on SLP_S0# assertion
	 * Bits 5:4 (11)  - Nominal output voltage: 0.850V
	 * Bits 3:2 (10)  - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10)  - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x30, 0x3a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x34, 0x2a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage 0.85V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x38, 0x2a);
}

static void board_pmic_enable_slp_s0_vr_decay(void)
{
	/*
	 * VCCIOCNT:
	 * Bit 6    (1)   - Enable decay of VCCIO on SLP_S0# assertion
	 * Bits 5:4 (11)  - Nominal output voltage: 0.850V
	 * Bits 3:2 (10)  - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10)  - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x30, 0x7a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x34, 0x6a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage 0.85V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x38, 0x6a);
}

__override void power_board_handle_host_sleep_event(enum host_sleep_event state)
{
	if (state == HOST_SLEEP_EVENT_S0IX_SUSPEND)
		board_pmic_enable_slp_s0_vr_decay();
	else if (state == HOST_SLEEP_EVENT_S0IX_RESUME)
		board_pmic_disable_slp_s0_vr_decay();
}

static void board_pmic_init(void)
{
	int pgmask1;

	/* Mask V5A_DS3_PG from PMIC PGMASK1. */
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x18, &pgmask1))
		return;
	pgmask1 |= BIT(2);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x18, pgmask1);

	board_pmic_disable_slp_s0_vr_decay();

	/* Enable active discharge (100 ohms) on V33A_PCH and V1.8A. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3D, 0x5);

	/* Enable active discharge (500 ohms) on 1.8U and (100 ohms) 1.2U. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3E, 0xD0);
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

static void board_quirks(void)
{
	/*
	 * Newer board revisions have external pull ups stuffed, so remove the
	 * internal pulls.
	 */
	if (board_get_version() > 0) {
		gpio_set_flags(GPIO_USB_C0_PD_INT_ODL, GPIO_INT_FALLING);
		gpio_set_flags(GPIO_USB_C1_PD_INT_ODL, GPIO_INT_FALLING);
	}

	/*
	 * Older boards don't have the SBU bypass circuitry needed for CCD, so
	 * enable the CCD_MODE_ODL interrupt such that we can help in making
	 * sure the SBU FETs are connected.
	 */
	if (board_get_version() < 2)
		gpio_enable_interrupt(GPIO_CCD_MODE_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_quirks, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	int lvl;

	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the levels are inverted because the pin is active low. */
	lvl = is_overcurrented ? 0 : 1;

	switch (port) {
	case 0:
		gpio_set_level(GPIO_USB_C0_OC_ODL, lvl);
		break;

	case 1:
		gpio_set_level(GPIO_USB_C1_OC_ODL, lvl);
		break;

	default:
		return;
	};
}

static int read_gyro_sensor_temp(int idx, int *temp_ptr)
{
	/*
	 * The gyro is only powered in S0, so don't go and read it if the AP is
	 * off.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return EC_ERROR_NOT_POWERED;

	return bmi160_get_sensor_temp(idx, temp_ptr);
}

const struct temp_sensor_t temp_sensors[] = {
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },

	/* These BD99992GW temp sensors are only readable in S0 */
	{ "Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM0 },
	{ "Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM1 },
	{ "DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM2 },
	{ "eMMC", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM3 },
	/* The Gyro temperature sensor is only readable in S0. */
	{ "Gyro", TEMP_SENSOR_TYPE_BOARD, read_gyro_sensor_temp, LID_GYRO }
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 }, /* Battery */
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 }, /* Ambient */
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 }, /* Charger */
	{ { 0, C_TO_K(52), 0 }, { 0, 0, 0 }, 0, 0 }, /* DRAM */
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 }, /* eMMC */
	{ { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 } /* Gyro */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/*
 * Check if PMIC fault registers indicate VR fault. If yes, print out fault
 * register info to console. Additionally, set panic reason so that the OS can
 * check for fault register info by looking at offset 0x14(PWRSTAT1) and
 * 0x15(PWRSTAT2) in cros ec panicinfo.
 */
static void board_report_pmic_fault(const char *str)
{
	int vrfault, pwrstat1 = 0, pwrstat2 = 0;
	uint32_t info;

	/* RESETIRQ1 -- Bit 4: VRFAULT */
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, &vrfault) !=
	    EC_SUCCESS)
		return;

	if (!(vrfault & BIT(4)))
		return;

	/* VRFAULT has occurred, print VRFAULT status bits. */

	/* PWRSTAT1 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, &pwrstat1);

	/* PWRSTAT2 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, &pwrstat2);

	CPRINTS("PMIC VRFAULT: %s", str);
	CPRINTS("PMIC VRFAULT: PWRSTAT1=0x%02x PWRSTAT2=0x%02x", pwrstat1,
		pwrstat2);

	/* Clear all faults -- Write 1 to clear. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, BIT(4));
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, pwrstat1);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, pwrstat2);

	/*
	 * Status of the fault registers can be checked in the OS by looking at
	 * offset 0x14(PWRSTAT1) and 0x15(PWRSTAT2) in cros ec panicinfo.
	 */
	info = ((pwrstat2 & 0xFF) << 8) | (pwrstat1 & 0xFF);
	panic_set_reason(PANIC_SW_PMIC_FAULT, info, 0);
}

void board_reset_pd_mcu(void)
{
	cprints(CC_USB, "Resetting TCPCs...");
	cflush();
	/* GPIO_USB_PD_RST_L resets all the TCPCs. */
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	crec_msleep(10); /* TODO(aaboagye): Verify min hold time. */
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
	crec_msleep(PS8805_FW_INIT_DELAY_MS);
}

void board_set_tcpc_power_mode(int port, int mode)
{
	/* Ignore the "mode" to turn the chip on.  We can only do a reset. */
	if (mode)
		return;

	board_reset_pd_mcu();
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int rv;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			rv = ppc_vbus_sink_enable(i, 0);
			/*
			 * Deliberately ignoring this error since it may cause
			 * an assertion error.
			 */
			if (rv)
				CPRINTS("Disabling p%d sink path failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTF("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

static void board_chipset_reset(void)
{
	board_report_pmic_fault("CHIPSET RESET");
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, board_chipset_reset, HOOK_PRIO_DEFAULT);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and PPC.  Therefore, go
	 * out and actually read the alert registers to report the alert status.
	 */
	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			/* The TCPCI spec says to ignore bits 14:12. */
			regval &= ~(BIT(14) | BIT(13) | BIT(12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			/* TCPCI spec says to ignore bits 14:12. */
			regval &= ~(BIT(14) | BIT(13) | BIT(12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}
