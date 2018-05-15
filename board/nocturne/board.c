/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nocturne board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "console.h"
#include "compile_time_macros.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_opt3001.h"
#include "driver/ppc/sn5s330.h"
#include "driver/sync.h"
#include "driver/tcpm/ps8xxx.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "system.h"
#include "system_chip.h"
#include "switch.h"
#include "task.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal s)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
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

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	[ADC_BASE_ATTACH] = {
		"BASE ATTACH", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},

	[ADC_BASE_DETACH] = {
		"BASE DETACH", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
};

/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SLP_S0_L,
	 POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
	 "SLP_S0_DEASSERTED"},
	{GPIO_SLP_S3_L,		POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_SLP_S4_L,		POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,	POWER_SIGNAL_ACTIVE_HIGH, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD,	POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,	POWER_SIGNAL_ACTIVE_HIGH, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_DB0_LED_RED] =   { 3, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB0_LED_GREEN] = { 0, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB0_LED_BLUE] =  { 2, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB1_LED_RED] =   { 7, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB1_LED_GREEN] = { 5, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB1_LED_BLUE] =  { 6, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{
		"battery", I2C_PORT_BATTERY, 100, GPIO_EC_I2C4_BATTERY_SCL,
		GPIO_EC_I2C4_BATTERY_SDA
	},

	{
		"power", I2C_PORT_POWER, 100, GPIO_EC_I2C0_POWER_SCL,
		GPIO_EC_I2C0_POWER_SDA
	},

	/* TODO(aaboagye): Restore 1MHz bus speed for after eval. */
	{
		"als_gyro", I2C_PORT_ALS_GYRO, 100, GPIO_EC_I2C5_ALS_GYRO_SCL,
		GPIO_EC_I2C5_ALS_GYRO_SDA
	},

	{
		"usbc0", I2C_PORT_USB_C0, 100, GPIO_USB_C0_SCL, GPIO_USB_C0_SDA
	},

	{
		"usbc1", I2C_PORT_USB_C1, 100, GPIO_USB_C1_SCL, GPIO_USB_C1_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);


/*
 * Motion Sense
 */

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

/* Sensor driver data */
static struct bmi160_drv_data_t g_bmi160_data;
static struct opt3001_drv_data_t g_opt3001_data = {
	.scale = 1,
	.uscale = 0,
	.offset = 0,
};

/* Matrix to rotate accel/gyro into standard reference frame. */
const matrix_3x3_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(-1),  0},
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "BMI160 ACC",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ALS_GYRO,
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 4, /* g, enough for laptop. */
		.min_frequency = BMI160_ACCEL_MIN_FREQ,
		.max_frequency = BMI160_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000,
				.ec_rate = 76 * MSEC,
			},
		},
	},

	[LID_GYRO] = {
		.name = "BMI160 GYRO",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ALS_GYRO,
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 1000, /* dps */
		.min_frequency = BMI160_GYRO_MIN_FREQ,
		.max_frequency = BMI160_GYRO_MAX_FREQ,
	},

	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_OPT3001,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &opt3001_drv,
		.drv_data = &g_opt3001_data,
		.port = I2C_PORT_ALS_GYRO,
		.addr = OPT3001_I2C_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1; uscale = 0 */
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

const struct ppc_config_t ppc_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
	{
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv,
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.i2c_host_port = I2C_PORT_USB_C0,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},

	{
		.i2c_host_port = I2C_PORT_USB_C1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

/* The port_addr members are PD port numbers, not I2C port numbers. */
struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},

	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
};

void board_chipset_startup(void)
{
	gpio_set_level(GPIO_EN_5V, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

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
		/* BRD_ID3 is LSb. */
		if (gpio_get_level(GPIO_EC_BRD_ID3))
			board_version |= 0x1;
		if (gpio_get_level(GPIO_EC_BRD_ID2))
			board_version |= 0x2;
		if (gpio_get_level(GPIO_EC_BRD_ID1))
			board_version |= 0x4;
		if (gpio_get_level(GPIO_EC_BRD_ID0))
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
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x49, 0x1);

	/* Wait for power to be cut. */
	while (1)
		;
}

static void board_init(void)
{
	/* Enable sensor interrupts. */
	gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);
	gpio_enable_interrupt(GPIO_RCAM_VSYNC);

	/* Enable USB Type-C interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_pmic_init(void)
{
	int pgmask1;

	/* Mask V5A_DS3_PG from PMIC PGMASK1. */
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x18, &pgmask1))
		return;
	pgmask1 |= (1 << 2);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x18, pgmask1);
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port)
{
	/* Sanity check the port. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_COUNT))
		return;

	/* Note that the levels are inverted because the pin is active low. */
	switch (port) {
	case 0:
		gpio_set_level(GPIO_USB_C0_OC_ODL, 0);
		break;

	case 1:
		gpio_set_level(GPIO_USB_C1_OC_ODL, 0);
		break;

	default:
		return;
	};

	/* TODO(b/69935262): Write a PD log entry for the OC event. */
	CPRINTS("C%d: overcurrent!", port);
}

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
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x8, &vrfault)
	    != EC_SUCCESS)
		return;

	if (!(vrfault & (1 << 4)))
		return;

	/* VRFAULT has occurred, print VRFAULT status bits. */

	/* PWRSTAT1 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x16, &pwrstat1);

	/* PWRSTAT2 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x17, &pwrstat2);

	CPRINTS("PMIC VRFAULT: %s", str);
	CPRINTS("PMIC VRFAULT: PWRSTAT1=0x%02x PWRSTAT2=0x%02x", pwrstat1,
		pwrstat2);

	/* Clear all faults -- Write 1 to clear. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x8, (1 << 4));
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x16, pwrstat1);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x17, pwrstat2);

	/*
	 * Status of the fault registers can be checked in the OS by looking at
	 * offset 0x14(PWRSTAT1) and 0x15(PWRSTAT2) in cros ec panicinfo.
	 */
	info = ((pwrstat2 & 0xFF) << 8) | (pwrstat1 & 0xFF);
	panic_set_reason(PANIC_SW_PMIC_FAULT, info, 0);
}

void board_reset_pd_mcu(void)
{
	/* GPIO_USB_PD_RST_L resets all the TCPCs. */
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	msleep(10); /* TODO(aaboagye): Verify min hold time. */
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

void board_rtc_reset(void)
{
	cprints(CC_CHIPSET, "Asserting RTCRST# to PCH");
	gpio_set_level(GPIO_EC_PCH_RTCRST, 1);
	udelay(100);
	gpio_set_level(GPIO_EC_PCH_RTCRST, 0);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_COUNT);
	int i;
	int rv;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

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

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
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
	if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
		/* The TCPCI spec says to ignore bits 14:12. */
		regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

		if (regval)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
		/* TCPCI spec says to ignore bits 14:12. */
		regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

		if (regval)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}
