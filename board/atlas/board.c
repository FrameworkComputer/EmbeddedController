/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atlas board-specific configuration */

#include "adc_chip.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/als_opt3001.h"
#include "driver/charger/isl923x.h"
#include "driver/pmic_bd99992gw.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "espi.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
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

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Keyboard scan. Increase output_settle_us to 80us from default 50us. */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { 3, 0, 10000 },
	[PWM_CH_DB0_LED_BLUE] = { 0, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				  2400 },
	[PWM_CH_DB0_LED_RED] = { 2, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				 2400 },
	[PWM_CH_DB0_LED_GREEN] = { 6, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
	[PWM_CH_DB1_LED_BLUE] = { 1, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				  2400 },
	[PWM_CH_DB1_LED_RED] = { 7, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				 2400 },
	[PWM_CH_DB1_LED_GREEN] = { 5, PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
				   2400 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Hibernate wake configuration */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_ROP_EC_ACOK,
	GPIO_LID_OPEN,
	GPIO_MECH_PWR_BTN_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = { "AMON_BMON", NPCX_ADC_CH2, ADC_MAX_VOLT * 1000 / 18,
			    ADC_READ_MAX + 1, 0 },
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 12.4K resistor, to read
	 * 0.8V @ 45 W, i.e. 56250 uW/mV. Using ADC_MAX_VOLT*56250 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = { "PSYS", NPCX_ADC_CH3,
		       ADC_MAX_VOLT * 56250 * 2 / (ADC_READ_MAX + 1), 2, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C0_POWER_SCL,
	  .sda = GPIO_EC_I2C0_POWER_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C1_USB_C0_SCL,
	  .sda = GPIO_EC_I2C1_USB_C0_SDA },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C2_USB_C1_SCL,
	  .sda = GPIO_EC_I2C2_USB_C1_SDA },
	{ .name = "sensor",
	  .port = I2C_PORT_SENSOR,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C3_SENSOR_3V3_SCL,
	  .sda = GPIO_EC_I2C3_SENSOR_3V3_SDA },
	{ .name = "battery",
	  .port = I2C_PORT_BATTERY,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C4_BATTERY_SCL,
	  .sda = GPIO_EC_I2C4_BATTERY_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Charger Chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		/* left port */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = I2C_ADDR_TCPC_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		/* right port */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = I2C_ADDR_TCPC_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
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

void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	crec_msleep(PS8XXX_RST_L_RST_H_DELAY_MS);
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

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

const struct temp_sensor_t temp_sensors[] = {
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },
	/* BD99992GW temp sensors are only readable in S0 */
	{ "Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM0 },
	{ "Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM1 },
	{ "DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM2 },
	{ "eMMC", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

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
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		      BD99992GW_REG_RESETIRQ1, &vrfault) != EC_SUCCESS)
		return;

	if (!(vrfault & BIT(4)))
		return;

	/* VRFAULT has occurred, print VRFAULT status bits. */

	/* PWRSTAT1 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, BD99992GW_REG_PWRSTAT1,
		  &pwrstat1);

	/* PWRSTAT2 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, BD99992GW_REG_PWRSTAT2,
		  &pwrstat2);

	CPRINTS("PMIC VRFAULT: %s", str);
	CPRINTS("PMIC VRFAULT: PWRSTAT1=0x%02x PWRSTAT2=0x%02x", pwrstat1,
		pwrstat2);

	/* Clear all faults -- Write 1 to clear. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_RESETIRQ1, BIT(4));
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_PWRSTAT1, pwrstat1);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_PWRSTAT2, pwrstat2);

	/*
	 * Status of the fault registers can be checked in the OS by looking at
	 * offset 0x14(PWRSTAT1) and 0x15(PWRSTAT2) in cros ec panicinfo.
	 */
	info = ((pwrstat2 & 0xFF) << 8) | (pwrstat1 & 0xFF);
	panic_set_reason(PANIC_SW_PMIC_FAULT, info, 0);
}

static void board_pmic_disable_slp_s0_vr_decay(void)
{
	/*
	 * VCCIOCNT:
	 * Bit 6    (0)   - Disable decay of VCCIO on SLP_S0# assertion
	 * Bits 5:4 (11)  - Nominal output voltage: 0.850V
	 * Bits 3:2 (10)  - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10)  - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_VCCIOCNT, 0x3a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, BD99992GW_REG_V18ACNT,
		   0x2a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage 0.85V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_V085ACNT, 0x2a);
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
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_VCCIOCNT, 0x7a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, BD99992GW_REG_V18ACNT,
		   0x6a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage 0.85V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_V085ACNT, 0x6a);
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
	board_report_pmic_fault("SYSJUMP");

	/* Clear power source events */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_PWRSRCINT, 0xff);

	/* Disable power button shutdown timer */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_PBCONFIG, 0x00);

	if (system_jumped_late())
		return;

	/* DISCHGCNT1 - enable 100 ohm discharge on VCCIO */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_DISCHGCNT1, 0x01);

	/*
	 * DISCHGCNT2 - enable 100 ohm discharge on
	 * V5.0A, V3.3DSW, V3.3A and V1.8A
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_DISCHGCNT2, 0x55);

	/*
	 * DISCHGCNT3 - enable 500 ohm discharge on
	 * V1.8U_2.5U
	 * DISCHGCNT3 - enable 100 ohm discharge on
	 * V12U, V1.00A, V0.85A
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_DISCHGCNT3, 0xd5);

	/* DISCHGCNT4 - enable 100 ohm discharge on V33S, V18S, V100S */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_DISCHGCNT4, 0x15);

	/* VRMODECTRL - disable low-power mode for all rails */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_VRMODECTRL, 0x1f);

	/* V5ADS3CNT - boost V5A_DS3 by 2% */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_V5ADS3CNT, 0x1a);

	board_pmic_disable_slp_s0_vr_decay();
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	int p;

	/* Configure PSL pins */
	for (p = 0; p < hibernate_wake_pins_used; p++)
		system_config_psl_mode(hibernate_wake_pins[p]);

	/*
	 * Enter PSL mode.  Note that on Atlas, simply enabling PSL mode does
	 * not cut the EC's power.  Therefore, we'll need to cut off power via
	 * the ROP PMIC afterwards.
	 */
	system_enter_psl_mode();

	/* Cut off DSW power via the ROP PMIC. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS,
		   BD99992GW_REG_SDWNCTRL, BD99992GW_SDWNCTRL_SWDN);

	/* Wait for power to be cut. */
	while (1)
		;
}

/* Initialize board. */
static void board_init(void)
{
	if (system_get_board_version() < ATLAS_REV_FIXED_EC_WP) {
		int dflags;

		CPRINTS("Applying EC_WP_L workaround");
		dflags = gpio_get_default_flags(GPIO_EC_WP_L);
		gpio_set_flags(GPIO_EC_WP_L, dflags | GPIO_PULL_UP);
	}

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

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
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/* check if we are sourcing VBUS on the port */
	int is_source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
							  GPIO_USB_C1_5V_EN);

	if (is_real_port && is_source) {
		CPRINTS("No charging from p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_EN_USB_C0_CHARGE_L, 1);
		gpio_set_level(GPIO_EN_USB_C1_CHARGE_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_EN_USB_C0_CHARGE_L :
					     GPIO_EN_USB_C1_CHARGE_L,
			       1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_EN_USB_C1_CHARGE_L :
					     GPIO_EN_USB_C0_CHARGE_L,
			       0);
	}

	return EC_SUCCESS;
}

static void board_charger_init(void)
{
	charger_set_input_current_limit(
		CHARGER_SOLO,
		PD_MAX_CURRENT_MA *
			(100 - CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT) / 100);
}
DECLARE_HOOK(HOOK_INIT, board_charger_init, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_KBD_BL_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_KBD_BL_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_reset(void)
{
	board_report_pmic_fault("CHIPSET RESET");
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, board_chipset_reset, HOOK_PRIO_DEFAULT);

int board_get_version(void)
{
	static int ver;

	if (!ver) {
		/*
		 * Read the board EC ID on the tristate strappings
		 * using ternary encoding: 0 = 0, 1 = 1, Hi-Z = 2
		 */
		uint8_t id0, id1, id2;

		id0 = gpio_get_ternary(GPIO_BOARD_VERSION1);
		id1 = gpio_get_ternary(GPIO_BOARD_VERSION2);
		id2 = gpio_get_ternary(GPIO_BOARD_VERSION3);

		ver = (id2 * 9) + (id1 * 3) + id0;
		CPRINTS("Board ID = %d", ver);
	}

	return ver;
}

static struct opt3001_drv_data_t g_opt3001_data = {
	.scale = 1,
	.uscale = 0,
	.offset = 0,
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_OPT3001,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &opt3001_drv,
		.drv_data = &g_opt3001_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = OPT3001_I2C_ADDR_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 0x2b11a1, /* from nocturne */
		.min_frequency = OPT3001_LIGHT_MIN_FREQ,
		.max_frequency = OPT3001_LIGHT_MAX_FREQ,
		.config = {
			/* Sensor on in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
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
