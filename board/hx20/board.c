/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Microchip Evaluation Board(EVB) with
 * MEC1701H 144-pin processor card.
 * EVB connected to Intel SKL RVP3 configured
 * for eSPI with Kabylake silicon.
 */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "bd99992gw.h"
#include "button.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "cypress5525.h"
#include "driver/als_opt3001.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/charger/isl9241.h"
#include "driver/tcpm/tcpci.h"
#include "extpower.h"
#include "fan.h"
#include "gpio_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "espi.h"
#include "lpc_chip.h"
#include "lpc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "pi3usb9281.h"
#include "peci.h"
#include "peci_customization.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "spi_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "driver/temp_sensor/f75303.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "espi.h"
#include "battery_smart.h"
#include "keyboard_scan.h"
#include "keyboard_8042.h"
#include "keyboard_8042_sharedlib.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define POWER_LIMIT_1_W	28

#ifdef CONFIG_BOARD_PRE_INIT
/*
 * Used to enable JTAG debug during development.
 * NOTE: If ARM Serial Wire Viewer not used then SWV pin can be
 * be disabled and used for another purpose. Change mode to
 * MCHP_JTAG_MODE_SWD.
 * For low power idle testing enable GPIO060 as function 2(48MHZ_OUT)
 * to check PLL is turning off in heavy sleep. Note, do not put GPIO060
 * in gpio.inc
 * GPIO060 is port 1 bit[16].
 */
void board_config_pre_init(void)
{

#ifdef CONFIG_CHIPSET_DEBUG
	MCHP_EC_JTAG_EN = MCHP_JTAG_ENABLE + MCHP_JTAG_MODE_SWD_SWV;
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_MCHP_48MHZ_OUT)
	gpio_set_alternate_function(1, 0x10000, 2);
#endif

	/* Disable BGPO function */
	MCHP_WEEK_TIMER_BGPO_POWER &= ~(BIT(0) | BIT(1) | BIT(2));
	/* Make sure BPGO reset is RESET_SYS */
	MCHP_WEEK_TIMER_BGPO_RESET &= ~(BIT(0) | BIT(1) | BIT(2));


}
#endif /* #ifdef CONFIG_BOARD_PRE_INIT */

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_DEASSERTED] = {
		SLP_S3_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		SLP_S4_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S4_DEASSERTED",
	},
	[X86_SLP_S5_DEASSERTED] = {
		SLP_S5_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S5_DEASSERTED",
	},
	[X86_SLP_SUS_DEASSERTED] = {
		GPIO_PCH_SLP_SUS_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_SUS_DEASSERTED",
	},
	[X86_PWR_3V5V_PG] = {
		GPIO_PWR_3V5V_PG,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PWR_3V5V_PG",
	},
	[X86_VCCIN_AUX_VR_PG] = {
		GPIO_VCCIN_AUX_VR_PG,
		POWER_SIGNAL_ACTIVE_HIGH,
		"VCCIN_AUX_VR_PG",
	},
	[X86_VR_PWRGD] = {
		GPIO_VR_PWRGD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"VR_PWRGD",
	}
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 0,
		.flags = PWM_CONFIG_OPEN_DRAIN,
	},
	[PWM_CH_KBL] = {
		.channel = 4,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB0_LED_RED] = {
		.channel = 5,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB0_LED_GREEN] = {
		.channel = 6,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB0_LED_BLUE] = {
		.channel = 7,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB1_LED_RED] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB1_LED_GREEN_EVT] = {
		.channel = 4,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB1_LED_GREEN] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB1_LED_BLUE] = {
		.channel = 8,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_RED] = {
		.channel = (MCHP_PWM_ID_MAX+0),
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_GREEN] = {
		.channel = (MCHP_PWM_ID_MAX+1),
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_BLUE] = {
		.channel = (MCHP_PWM_ID_MAX+2),
		.flags = PWM_CONFIG_DSLEEP,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

#ifdef HAS_TASK_PDCMD
/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);

}
#endif

#include "gpio_list.h"

/* ADC channels
 * name, factor multiplier, factor divider, shift, channel
 */
const struct adc_t adc_channels[] = {
	[ADC_I_ADP]           = {"I_ADP", 3300, 4096, 0, 0},
	[ADC_I_SYS]           = {"I_SYS", 3300, 4096, 0, 1},
	[ADC_VCIN1_BATT_TEMP] = {"BATT_PRESENT", 3300, 4096, 0, 2},
	[ADC_TP_BOARD_ID]     = {"TP_BID", 3300, 4096, 0, 3},
	[ADC_AD_BID]          = {"AD_BID", 3300, 4096, 0, 4},
	[ADC_AUDIO_BOARD_ID]  = {"AUDIO_BID", 3300, 4096, 0, 5},
	[ADC_PROCHOT_L]       = {"PROCHOT_L", 3300, 4096, 0, 6}

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/*
 * MCHP EVB connected to KBL RVP3
 */
const struct i2c_port_t i2c_ports[]  = {
	{"batt",     MCHP_I2C_PORT1, 100,  GPIO_I2C_1_SDA, GPIO_I2C_1_SCL},
	{"touchpd",  MCHP_I2C_PORT2, 100,  GPIO_I2C_2_SDA, GPIO_I2C_2_SCL},
	{"sensors",  MCHP_I2C_PORT3, 100,  GPIO_I2C_3_SDA, GPIO_I2C_3_SCL},
	{"pd",       MCHP_I2C_PORT6, 100,  GPIO_I2C_6_SDA, GPIO_I2C_6_SCL}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*
 * Map ports to controller.
 * Ports may map to the same controller.
 */
const uint16_t i2c_port_to_ctrl[I2C_PORT_COUNT] = {
	(MCHP_I2C_CTRL0 << 8) + MCHP_I2C_PORT6,
	(MCHP_I2C_CTRL1 << 8) + MCHP_I2C_PORT1,
	(MCHP_I2C_CTRL1 << 8) + MCHP_I2C_PORT3,
	(MCHP_I2C_CTRL2 << 8) + MCHP_I2C_PORT2,
};

/*
 * default to I2C0 because callers may not check
 * return value if we returned an error code.
 */
int board_i2c_p2c(int port)
{
	int i;

	for (i = 0; i < I2C_PORT_COUNT; i++)
		if ((i2c_port_to_ctrl[i] & 0xFF) == port)
			return (int)(i2c_port_to_ctrl[i] >> 8);

	return -1;
}
#ifdef I2C_SLAVE
const uint32_t i2c_ctrl_slave_addrs[I2C_CONTROLLER_COUNT] = {
#ifdef CONFIG_BOARD_MCHP_I2C0_SLAVE_ADDRS
	(MCHP_I2C_CTRL0 + (CONFIG_BOARD_MCHP_I2C0_SLAVE_ADDRS << 16)),
#else
	(MCHP_I2C_CTRL0 + (CONFIG_MCHP_I2C0_SLAVE_ADDRS << 16)),
#endif
#ifdef CONFIG_BOARD_MCHP_I2C1_SLAVE_ADDRS
	(MCHP_I2C_CTRL1 + (CONFIG_BOARD_MCHP_I2C1_SLAVE_ADDRS << 16)),
#else
	(MCHP_I2C_CTRL1 + (CONFIG_MCHP_I2C1_SLAVE_ADDRS << 16)),
#endif
};

/* Return the two slave addresses the specified
 * controller will respond to when controller
 * is acting as a slave.
 * b[6:0]  = b[7:1] of I2C address 1
 * b[14:8] = b[7:1] of I2C address 2
 * When not using I2C controllers as slaves we can use
 * the same value for all controllers. The address should
 * not be 0x00 as this is the general call address.
 */
uint16_t board_i2c_slave_addrs(int controller)
{
	int i;

	for (i = 0; i < I2C_CONTROLLER_COUNT; i++)
		if ((i2c_ctrl_slave_addrs[i] & 0xffff) == controller)
			return (i2c_ctrl_slave_addrs[i] >> 16);

	return CONFIG_MCHP_I2C0_SLAVE_ADDRS;
}
#endif

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ QMSPI0_PORT, 4, GPIO_QMSPI_CS0},
#if defined(CONFIG_SPI_ACCEL_PORT)
	{ GPSPI0_PORT, 2, GPIO_SPI0_CS0 },
#endif
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
	GPIO_AC_PRESENT,
	GPIO_ON_OFF_BTN_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);


/*
 * Deep sleep support, called by chip level.
 */
#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_BOARD_DEEP_SLEEP)

/*
 * Perform any board level prepare for sleep actions.
 * For example, disabling pin/pads to further reduce
 * current during sleep.
 */
void board_prepare_for_deep_sleep(void)
{
#if defined(CONFIG_GPIO_POWER_DOWN) && \
	defined(CONFIG_MCHP_DEEP_SLP_GPIO_PWR_DOWN)
	gpio_power_down_module(MODULE_SPI_FLASH);
	gpio_power_down_module(MODULE_SPI_MASTER);
	gpio_power_down_module(MODULE_I2C);
	/* powering down keyscan is causing an issue with keyscan task
	 * probably due to spurious interrupts on keyscan pins.
	 * gpio_config_module(MODULE_KEYBOARD_SCAN, 0);
	 */

#ifndef CONFIG_POWER_S0IX
	gpio_power_down_module(MODULE_LPC);
#endif
#endif
}

/*
 * Perform any board level resume from sleep actions.
 * For example, re-enabling pins powered off in
 * board_prepare_for_deep_sleep().
 */
void board_resume_from_deep_sleep(void)
{
#if defined(CONFIG_GPIO_POWER_DOWN) && \
	defined(CONFIG_MCHP_DEEP_SLP_GPIO_PWR_DOWN)
#ifndef CONFIG_POWER_S0IX
	gpio_config_module(MODULE_LPC, 1);
#endif
	/* gpio_config_module(MODULE_KEYBOARD_SCAN, 1); */
	gpio_config_module(MODULE_SPI_FLASH, 1);
	gpio_config_module(MODULE_SPI_MASTER, 1);
	gpio_config_module(MODULE_I2C, 1);
#endif
}
#endif


/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{

}

static void vci_init(void)
{
	/**
	 * Switch VCI control from VCI_OUT to GPIO Pin Control
	 * These have to be done in sequence to prevent glitching
	 * the output pin
	 */
	MCHP_VCI_REGISTER |= MCHP_VCI_REGISTER_FW_CNTRL;
	MCHP_VCI_REGISTER |= MCHP_VCI_REGISTER_FW_EXT;
	/**
	 * only enable input for fp, powerbutton for now
	 */
	MCHP_VCI_INPUT_ENABLE = BIT(0) |  BIT(1);
	/* todo implement chassis open  detection*/
	/*MCHP_VCI_LATCH_ENABLE = BIT(0) | BIT(1) | BIT(2);*/
}
DECLARE_HOOK(HOOK_INIT, vci_init, HOOK_PRIO_FIRST);

/**
 * We should really really use mchp/system.c hibernate function.
 * however for now the EE design does not allow us to keep the EC on
 * without also keeping on the 5v3v ALW supplies, so we just wack
 * power to ourselves.
 */
static void board_power_off_deferred(void)
{
	int i;

	/* Disable interrupts */
	interrupt_disable();
	for (i = 0; i < MCHP_IRQ_MAX; ++i) {
		task_disable_irq(i);
		task_clear_pending_irq(i);
	}
	gpio_set_level(GPIO_VS_ON, 0);
	MCHP_VCI_REGISTER &= ~(MCHP_VCI_REGISTER_FW_CNTRL + MCHP_VCI_REGISTER_FW_EXT);
		/* Wait for power rails to die */
	while (1)
		;
}
DECLARE_DEFERRED(board_power_off_deferred);

void board_power_off(void)
{
	CPRINTS("Shutting down system in 30 seconds!");

	hook_call_deferred(&board_power_off_deferred_data, 30000 * MSEC);
}

void cancel_board_power_off(void)
{
	CPRINTS("Cancel shutdown");
	hook_call_deferred(&board_power_off_deferred_data, -1);
}

/**
 * Notify PCH of the AC presence.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_AC_PRESENT_OUT, extpower_is_present());

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) && !extpower_is_present()) {
		/* if AC disconnected, need to power_off EC_ON */
		board_power_off();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);


/* Initialize board. */
static void board_init(void)
{
	CPRINTS("MEC1701 HOOK_INIT - called board_init");
	board_get_version();

	gpio_enable_interrupt(GPIO_SOC_ENBKL);
	gpio_enable_interrupt(GPIO_ON_OFF_BTN_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_EMI_REGION1

static void sci_enable(void);
DECLARE_DEFERRED(sci_enable);

static void sci_enable(void)
{
	if (*host_get_customer_memmap(0x00) == 1) {
	/* when host set EC driver ready flag, EC need to enable SCI */
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, SCI_HOST_EVENT_MASK);

		update_soc_power_limit();
	} else
		hook_call_deferred(&sci_enable_data, 250 * MSEC);
}
#endif

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	CPRINTS("HOOK_CHIPSET_STARTUP - called board_chipset_startup");

#ifdef CONFIG_EMI_REGION1
	hook_call_deferred(&sci_enable_data, 250 * MSEC);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP,
		board_chipset_startup,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	CPRINTS(" HOOK_CHIPSET_SHUTDOWN board_chipset_shutdown");

#ifdef CONFIG_EMI_REGION1
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN,
		board_chipset_shutdown,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	CPRINTS("HOOK_CHIPSET_RESUME");
	/*gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);*/
	gpio_set_level(GPIO_EC_MUTE_L, 1);
	gpio_set_level(GPIO_CAM_EN, 1);
	gpio_set_level(GPIO_ME_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume,
	     MOTION_SENSE_HOOK_PRIO-1);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	CPRINTS("HOOK_CHIPSET_SUSPEND");
	/*gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);*/
	if (power_get_state() == POWER_S0S3) {
		gpio_set_level(GPIO_EC_MUTE_L, 0);
		gpio_set_level(GPIO_CAM_EN, 0);
		gpio_set_level(GPIO_ME_EN, 0);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND,
		board_chipset_suspend,
		HOOK_PRIO_DEFAULT);

void board_hibernate_late(void)
{
	/* put host chipset into reset */
	gpio_set_level(GPIO_SYS_RESET_L, 0);

	/* Turn off LEDs in hibernate */
	/*
	gpio_set_level(GPIO_CHARGE_LED_1, 0);
	gpio_set_level(GPIO_CHARGE_LED_2, 0);
	*/

	/*
	 * Set PD wake low so that it toggles high to generate a wake
	 * event once we leave hibernate.
	 */
	/*
	gpio_set_level(GPIO_USB_PD_WAKE, 0);
	*/
}


void soc_signal_interrupt(enum gpio_signal signal)
{
	/* TODO: EC BKOFF signal is related soc enable panel siganl */
	gpio_set_level(GPIO_EC_BKOFF_L,
		gpio_get_level(GPIO_SOC_ENBKL) ? 1 : 0);
}

void chassis_control_interrupt(enum gpio_signal signal)
{
	/* TODO: implement c cover open/close behavior
	 * When c cover close, drop the EC_ON to tune off EC power
	 */
}

void touchpad_interrupt(enum gpio_signal signal)
{
	/* TODO: implement touchpad process
	 *
	 */
}

struct {
	enum hx20_board_version version;
	int thresh_mv;
} const hx20_board_versions[] = {
	/* Vin = 3.3V, Ideal voltage */
	{ BOARD_VERSION_0, 203 },  /* 100 mV, 0 Kohm */
	{ BOARD_VERSION_1, 409 },  /* 310 mV, Kohm */
	{ BOARD_VERSION_2, 615 },  /* 520 mV, Kohm */
	{ BOARD_VERSION_3, 821 },  /* 720 mV, Kohm */
	{ BOARD_VERSION_4, 1028},   /* 930 mV, Kohm */
	{ BOARD_VERSION_5, 1234 }, /* 1130 mV, Kohm */
	{ BOARD_VERSION_6, 1440 }, /* 1340 mV, Kohm */
	{ BOARD_VERSION_7, 1646 }, /* 1550 mV, Kohm */
	{ BOARD_VERSION_8, 1853 }, /* 1750 mV, Kohm */
	{ BOARD_VERSION_9, 2059 }, /* 1960 mV, Kohm */
	{ BOARD_VERSION_10, 2265 }, /* 2170 mV, Kohm */
	{ BOARD_VERSION_11, 2471 }, /* 2370 mV, Kohm */
	{ BOARD_VERSION_12, 2678 }, /* 2580 mV, Kohm */
	{ BOARD_VERSION_13, 2884 }, /* 2780 mV, Kohm */
	{ BOARD_VERSION_14, 3090 }, /* 2990 mV, Kohm */
	{ BOARD_VERSION_15, 3300 }, /* 3300 mV, Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(hx20_board_versions) == BOARD_VERSION_COUNT);

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv;
	int i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	mv = adc_read_channel(ADC_AD_BID);

	if (mv == ADC_READ_ERROR)
		return BOARD_VERSION_UNKNOWN;

	for (i = 0; i < BOARD_VERSION_COUNT; i++)
		if (mv < hx20_board_versions[i].thresh_mv) {
			version = hx20_board_versions[i].version;
			return version;
		}

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
		0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xff,
		0xff, 0xff, 0x03, 0xff, 0xff, 0x03, 0xff, 0xff, 0xef  /* full set */
	},
};




/* Charger chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

#ifdef CONFIG_CHARGER_CUSTOMER_SETTING
static void charger_chips_init(void)
{
	/* Battery present need ADC function ready, so change the initail priority
	 * after ADC
	 */

	int chip;
	uint16_t val = 0x0000; /*default ac setting */

	for (chip = 0; chip < board_get_charger_chip_count(); chip++) {
		if (chg_chips[chip].drv->init)
			chg_chips[chip].drv->init(chip);
	}

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL2, ISL9241_CONTROL2_TRICKLE_CHG_CURR_128 |
			ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR |
			ISL9241_CONTROL2_PROCHOT_DEBOUNCE_100))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3, ISL9241_CONTROL3_PSYS_GAIN |
			ISL9241_CONTROL3_ACLIM_RELOAD))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6800 | ISL9241_CONTROL1_SWITCH_FREQ;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, val))
		goto init_fail;

	return;

init_fail:
	CPRINTF("ISL9241 customer init failed!");
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_INIT_ADC + 1);

void charger_update(void)
{
	static int pre_ac_state;
	static int pre_dc_state;
	uint16_t val = 0x0000;

	if (pre_ac_state != extpower_is_present() ||
		pre_dc_state != battery_is_present())
	{
		CPRINTS("update charger!!");

		val = ISL9241_CONTROL1_PROCHOT_REF_6800 |
				ISL9241_CONTROL1_SWITCH_FREQ | ISL9241_CONTROL1_PSYS;

		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, val)) {
			CPRINTS("Update charger control1 fail");
		}

		/* Set DC prochot to 6.912A */
		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_DC_PROCHOT, 0x1B00))
			CPRINTS("Update DC prochot fail");

		pre_ac_state = extpower_is_present();
		pre_dc_state = battery_is_present();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);

#endif

void update_soc_power_limit(void)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	int active_power;
	int pps_power_budget;
	int battery_percent;
	int pl2_watt = 0;
	int pl4_watt = 0;
	int psys_watt = 0;

	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psys_watt = -1;

	/* TODO: get the power and pps_power_budget */
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw()/1000000;
	pps_power_budget = cypd_get_pps_power_budget();

	if (!extpower_is_present() || (active_power < 55)) {
		/* Battery only or ADP < 55W */
		pl2_watt = POWER_LIMIT_1_W;
		pl4_watt = 70 - pps_power_budget;
		psys_watt = 52 - pps_power_budget;
	} else if (battery_percent < 30) {
		/* ADP > 55W and Battery percentage < 30% */
		pl4_watt = active_power - 15 - pps_power_budget;
		pl2_watt = MIN((pl4_watt * 90) / 100, 64);
		psys_watt = ((active_power * 95) / 100) - pps_power_budget;
	} else {
		/* ADP > 55W and Battery percentage >= 30% */
		pl2_watt = 64;
		pl4_watt = 121;
		/* psys watt = adp watt * 0.95 + battery watt(55 W) * 0.7 - pps power budget */
		psys_watt = ((active_power * 95) / 100) + 39 - pps_power_budget;
	}
	if (pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt || psys_watt != old_psys_watt) {
		old_psys_watt = psys_watt;
		old_pl4_watt = pl4_watt;
		old_pl2_watt = pl2_watt;
		CPRINTS("Updating SOC Power Limits: PL2 %d, PL4 %d, Psys %d, Adapter %d", 
				pl2_watt, pl4_watt, psys_watt, active_power);
		peci_update_PL1(POWER_LIMIT_1_W);
		peci_update_PL2(pl2_watt);
		peci_update_PL4(pl4_watt);
		peci_update_PsysPL2(psys_watt);
	}


}
DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit, HOOK_PRIO_DEFAULT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_LOCAL] = {
		.name = "F75303_Local",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = f75303_get_val,
		.idx = F75303_IDX_LOCAL
	},
	[TEMP_SENSOR_CPU] = {
		.name = "F75303_CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = f75303_get_val,
		.idx = F75303_IDX_REMOTE2
	},
	[TEMP_SENSOR_DDR] = {
		.name = "F75303_DDR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = f75303_get_val,
		.idx = F75303_IDX_REMOTE1
	},
	[TEMP_SENSOR_BATTERY] = {
		.name = "Battery",
		.type = TEMP_SENSOR_TYPE_BATTERY,
		.read = charge_get_battery_temp,
		.idx = 0
	},
#ifdef CONFIG_PECI
	[TEMP_SENSOR_PECI] = {
		.name = "PECI",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = peci_temp_sensor_get_val,
		.idx = 0,
	},
#endif /* CONFIG_PECI */
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#ifdef CONFIG_FANS
/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 0,	/* Use MFT id to control fan */
	.pgood_gpio = GPIO_PWR_3V5V_PG,
	.enable_gpio = -1,
};

/* Default */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1800,
	.rpm_start = 1800,
	.rpm_max = 6000, /* Todo: Derate by -7% so all units have same performance */
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

/*
 * Inductor limits - used for both charger and regulator
 *
 * Need to use the lower of the charger IC, regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * ISL9241 regulator: operating range -40 C to 125 C
 *
 * Inductors: limit of ?C
 * PCB: limit is 80c
 */
const static struct ec_thermal_config thermal_inductor = {
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
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(55),
};

const static struct ec_thermal_config thermal_battery = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(50),
		[EC_TEMP_THRESH_HALT] = C_TO_K(60),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(40),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(50),
};
#ifdef CONFIG_PECI
const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(95),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
		[EC_TEMP_THRESH_HALT] = C_TO_K(101),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(55),
	.temp_fan_max = C_TO_K(90),
};
#endif

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_LOCAL] = thermal_inductor;
	thermal_params[TEMP_SENSOR_CPU] = thermal_inductor;
	thermal_params[TEMP_SENSOR_DDR] = thermal_inductor;
	thermal_params[TEMP_SENSOR_BATTERY] = thermal_battery;
#ifdef CONFIG_PECI
	thermal_params[TEMP_SENSOR_PECI] = thermal_cpu;
#endif
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);
#endif

static int prochot_low_time;
void prochot_monitor(void)
{
	int val_l;
	/* TODO Enable this once PROCHOT has moved to VCCIN_AUX_CORE_ALERT#_R
	* Right now the voltage for this is too low for us to sample using gpio.
	*/
	val_l = adc_read_channel(ADC_PROCHOT_L) > 500;
	/*val_l = gpio_get_level(GPIO_EC_val_lPROCHOT_L);*/
	if (val_l) {
		prochot_low_time = 0;
	} else {
		prochot_low_time++;
		if ((prochot_low_time & 0xF) == 0xF && chipset_in_state(CHIPSET_STATE_ON))
			CPRINTF("PROCHOT has been low for too long - investigate");
	}
}
DECLARE_HOOK(HOOK_SECOND, prochot_monitor, HOOK_PRIO_DEFAULT);



int mainboard_power_button_first_state;
static void mainboard_power_button_change_deferred(void)
{
	if (mainboard_power_button_first_state == gpio_get_level(GPIO_ON_OFF_BTN_L)) {
		CPRINTF("Got Mainboard Power Button event");
		power_button_set_simulated_state(!gpio_get_level(GPIO_ON_OFF_BTN_L));
	}
}
DECLARE_DEFERRED(mainboard_power_button_change_deferred);

void mainboard_power_button_interrupt(enum gpio_signal signal)
{
	mainboard_power_button_first_state = gpio_get_level(GPIO_ON_OFF_BTN_L);
	hook_call_deferred(&mainboard_power_button_change_deferred_data,
			   50);
}




static int cmd_spimux(int argc, char **argv)
{
	int enable;
	if (argc == 2) {
		if (!parse_bool(argv[1], &enable))
			return EC_ERROR_PARAM1;

		if (enable){
			/* Disable LED drv */
			gpio_set_level(GPIO_TYPEC_G_DRV2_EN, 0);
			/* Set GPIO56 as SPI for access SPI ROM */
			gpio_set_alternate_function(1, 0x4000, 2);
		} else {
			/* Enable LED drv */
			gpio_set_level(GPIO_TYPEC_G_DRV2_EN, 1);
			/* Set GPIO56 as SPI for access SPI ROM */
			gpio_set_alternate_function(1, 0x4000, 1);
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(spimux, cmd_spimux,
			"[enable/disable]",
			"Set if spi CLK is in SPI mode (true) or PWM mode");

int fingerprint_power_button_first_state;
static void fingerprint_power_button_change_deferred(void)
{
	if (fingerprint_power_button_first_state == gpio_get_level(GPIO_ON_OFF_FP_L))
		factory_power_button(!gpio_get_level(GPIO_ON_OFF_FP_L));
}
DECLARE_DEFERRED(fingerprint_power_button_change_deferred);

void fingerprint_power_button_interrupt(enum gpio_signal signal)
{
	if (!factory_status())
		power_button_interrupt(signal);
	else {
		fingerprint_power_button_first_state = gpio_get_level(GPIO_ON_OFF_FP_L);
		hook_call_deferred(&fingerprint_power_button_change_deferred_data,
				50);
	}
}
