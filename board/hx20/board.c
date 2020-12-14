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


/* NOTE: MEC17xx EVB + SKL RVP3 does not use BD99992 PMIC.
 * RVP3 PMIC controlled by RVP3 logic.
 */
#define I2C_ADDR_BD99992_FLAGS	0x30

/*
 * Maxim DS1624 I2C temperature sensor used for testing I2C.
 * DS1624 contains one internal temperature sensor
 * and EEPROM. It has no external temperature inputs.
 */
#define DS1624_I2C_ADDR_FLAGS	(0x48 | I2C_FLAG_BIG_ENDIAN)
#define DS1624_IDX_LOCAL	0
#define DS1624_READ_TEMP16	0xAA	/* read 16-bit temperature */
#define DS1624_ACCESS_CFG	0xAC	/* read/write 8-bit config */
#define DS1624_CMD_START	0xEE
#define DS1624_CMD_STOP		0x22

static int forcing_shutdown;  /* Forced shutdown in progress? */

#ifdef HAS_TASK_MOTIONSENSE
static void board_spi_enable(void);
static void board_spi_disable(void);
#endif

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
#ifdef CONFIG_POWER_S0IX
	[X86_SLP_S0_DEASSERTED] = {
		GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED",
	},
#endif
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

#ifdef CONFIG_PWM
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 0,
		.flags = PWM_CONFIG_OPEN_DRAIN,
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
	[PWM_CH_DB1_LED_GREEN] = {
		.channel = 4,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_DB1_LED_BLUE] = {
		.channel = 8,
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_RED] = {
		.channel = (MCHP_PWM_ID_MAX+1),
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_GREEN] = {
		.channel = (MCHP_PWM_ID_MAX+0),
		.flags = PWM_CONFIG_DSLEEP,
	},
	[PWM_CH_FPR_LED_BLUE] = {
		.channel = (MCHP_PWM_ID_MAX+2),
		.flags = PWM_CONFIG_DSLEEP,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
#endif

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * If eSPI_Reset# pin is asserted without SLP_SUS# being asserted, then
	 * it means that there is an unexpected power loss (global reset
	 * event). In this case, check if shutdown was being forced by pressing
	 * power button. If yes, release power button.
	 */
	if ((power_get_signals() & IN_PCH_SLP_SUS_DEASSERTED) &&
		forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}
}
/*
 * Use EC to handle ALL_SYS_PWRGD signal.
 * MEC17xx connected to SKL/KBL RVP3 reference board
 * is required to monitor ALL_SYS_PWRGD and drive SYS_RESET_L
 * after a 10 to 100 ms delay.
 */
#ifdef CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD

static void board_all_sys_pwrgd(void)
{
	int allsys_in = gpio_get_level(GPIO_ALL_SYS_PWRGD);
	int allsys_out = gpio_get_level(GPIO_SYS_RESET_L);

	if (allsys_in == allsys_out)
		return;

	CPRINTS("ALL_SYS_PWRGD=%d SYS_RESET_L=%d", allsys_in, allsys_out);

	trace2(0, BRD, 0, "ALL_SYS_PWRGD=%d SYS_RESET_L=%d",
			allsys_in, allsys_out);

	/*
	 * Wait at least 10 ms between power signals going high
	 */
	if (allsys_in)
		msleep(100);

	if (!allsys_out) {
		/* CPRINTS("Set SYS_RESET_L = %d", allsys_in); */
		trace1(0, BRD, 0, "Set SYS_RESET_L=%d", allsys_in);
		gpio_set_level(GPIO_SYS_RESET_L, allsys_in);
		/* Force fan on for kabylake RVP */
		gpio_set_level(GPIO_EC_FAN1_PWM, 1);
	}
}
DECLARE_DEFERRED(board_all_sys_pwrgd);

void all_sys_pwrgd_interrupt(enum gpio_signal signal)
{
	trace0(0, ISR, 0, "ALL_SYS_PWRGD Edge");
	hook_call_deferred(&board_all_sys_pwrgd_data, 0);
}
#endif /* #ifdef CONFIG_BOARD_HAS_ALL_SYS_PWRGD */


#ifdef HAS_TASK_PDCMD
/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);

}
#endif

#ifdef CONFIG_USB_POWER_DELIVERY
void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
}

void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}
#endif

/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, 0);
}

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
	[ADC_AUDIO_BOARD_ID]  = {"AUDIO_BID", 3300, 4096, 0, 5}
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
	(MCHP_I2C_CTRL1 << 8) + MCHP_I2C_PORT1,
	(MCHP_I2C_CTRL2 << 8) + MCHP_I2C_PORT2,
	(MCHP_I2C_CTRL3 << 8) + MCHP_I2C_PORT3,
	(MCHP_I2C_CTRL0 << 8) + MCHP_I2C_PORT6
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

#ifdef CONFIG_USB_POWER_DELIVERY
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{I2C_PORT_TCPC,
	 CONFIG_TCPC_I2C_BASE_ADDR_FLAGS,
	 &tcpci_tcpm_drv},

	{I2C_PORT_TCPC,
	 CONFIG_TCPC_I2C_BASE_ADDR_FLAGS + 1,
	 &tcpci_tcpm_drv},
};
#endif

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
	GPIO_ON_OFF_BTN_L,
	GPIO_TYPEC0_VBUS_ON_EC,
	GPIO_TYPEC1_VBUS_ON_EC,
	GPIO_TYPEC2_VBUS_ON_EC,
	GPIO_TYPEC3_VBUS_ON_EC,
	GPIO_EC_PD_INTA_L,
	GPIO_EC_PD_INTB_L
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

#ifdef CONFIG_USB_MUX_PI3USB30532
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_2,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
		.driver = &pi3usb3x532_usb_mux_driver,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = 0x10,
		.driver = &ps874x_usb_mux_driver,
	}
};
#endif

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	// TODO FRAMEWORK
	//gpio_set_level(GPIO_PD_RST_L, 0);
	usleep(100);
	//gpio_set_level(GPIO_PD_RST_L, 1);
}


#ifdef CONFIG_ALS
/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{"TI", opt3001_init, opt3001_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);
#endif

/*
TODO FRAMEWORK 
const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L,
	 30 * MSEC, 0},
};
*/

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
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);


/* Initialize board. */
static void board_init(void)
{
	CPRINTS("MEC1701 HOOK_INIT - called board_init");
	trace0(0, HOOK, 0, "HOOK_INIT - call board_init");
	board_get_version();



	gpio_enable_interrupt(GPIO_SOC_ENBKL);
	gpio_enable_interrupt(GPIO_ON_OFF_BTN_L);


#ifdef CONFIG_USB_POWER_DELIVERY
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);
#endif


#ifdef HAS_TASK_MOTIONSENSE
	if (system_jumped_to_this_image() &&
	    chipset_in_state(CHIPSET_STATE_ON)) {
		trace0(0, BRD, 0, "board_init: S0 call board_spi_enable");
		board_spi_enable();
	}
#endif


}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);


#ifdef CONFIG_CHARGER
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
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/* check if we are source vbus on that port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTS("MEC1701 Skip enable p%d", charge_port);
		trace1(0, BOARD, 0, "Skip enable charge port %d",
			charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("MEC1701 New chg p%d", charge_port);
	trace1(0, BOARD, 0, "New charge port %d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_EN_L :
					     GPIO_USB_C1_CHARGE_EN_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_EN_L :
					     GPIO_USB_C0_CHARGE_EN_L, 0);
	}

	return EC_SUCCESS;
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
	charge_set_input_current_limit(MAX(charge_ma,
				   CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}
#else
/*
 * TODO HACK providing functions from common/charge_state_v2.c
 * which is not compiled in when no charger
 */
int charge_want_shutdown(void)
{
	return 0;
}

int charge_prevent_power_on(int power_button_pressed)
{
	return 0;
}


#endif

/*
 * Enable or disable input devices,
 * based upon chipset state and tablet mode
 */
static void enable_input_devices(void)
{
	int kb_enable = 1;
	int tp_enable = 1;

	/* Disable both TP and KB in tablet mode */
	/* if (!gpio_get_level(GPIO_TABLET_MODE_L))
		kb_enable = tp_enable = 0; 
	*/
	/* Disable TP if chipset is off */
	// TODO FRAMEWORK else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		tp_enable = 0;

	keyboard_scan_enable(kb_enable, KB_SCAN_DISABLE_LID_CLOSED);
	gpio_set_level(GPIO_ENABLE_TOUCHPAD, tp_enable);
}

#ifdef CONFIG_EMI_REGION1

static void sci_enable(void);
DECLARE_DEFERRED(sci_enable);

static void sci_enable(void)
{
	if (*host_get_customer_memmap(0x00) == 1) {
	/* when host set EC driver ready flag, EC need to enable SCI */
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0x10AF92AFF);
	} else
		hook_call_deferred(&sci_enable_data, 250 * MSEC);
}
#endif

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_STARTUP - called board_chipset_startup");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_STARTUP - board_chipset_startup");
	/* TODO FRAMEWORK 
	gpio_set_level(GPIO_USB1_ENABLE, 1);
	gpio_set_level(GPIO_USB2_ENABLE, 1);
	*/ 
#ifdef CONFIG_EMI_REGION1
	hook_call_deferred(&sci_enable_data, 250 * MSEC);
#endif
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP,
		board_chipset_startup,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_SHUTDOWN board_chipset_shutdown");
	trace0(0, HOOK, 0,
	       "HOOK_CHIPSET_SHUTDOWN board_chipset_shutdown");
	/* TODO FRAMEWORK 
	gpio_set_level(GPIO_USB1_ENABLE, 0);
	gpio_set_level(GPIO_USB2_ENABLE, 0);
	*/ 
#ifdef CONFIG_EMI_REGION1
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
#endif
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN,
		board_chipset_shutdown,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	CPRINTS("MEC1701_EVG HOOK_CHIPSET_RESUME");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_RESUME - board_chipset_resume");
	/*gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);*/
	gpio_set_level(GPIO_EC_MUTE_L, 1);
	gpio_set_level(GPIO_EC_WLAN_EN,1);
	gpio_set_level(GPIO_EC_WL_OFF_L,1);
	gpio_set_level(GPIO_CAM_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume,
	     MOTION_SENSE_HOOK_PRIO-1);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_SUSPEND - called board_chipset_resume");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_SUSPEND - board_chipset_suspend");
	/*gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);*/
	gpio_set_level(GPIO_EC_MUTE_L, 0);
	gpio_set_level(GPIO_EC_WLAN_EN,1);
	gpio_set_level(GPIO_EC_WL_OFF_L,1);
	gpio_set_level(GPIO_CAM_EN, 0);
#if 0 /* TODO not implemented in gpio.inc */
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 0);
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 0);
#endif
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

#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
	/*
	 * Leave USB-C charging enabled in hibernate, in order to
	 * allow wake-on-plug. 5V enable must be pulled low.
	 */
#if CONFIG_USB_PD_PORT_MAX_COUNT > 0
	gpio_set_flags(GPIO_USB_C0_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
	gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 0);
#endif
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	gpio_set_flags(GPIO_USB_C1_5V_EN, GPIO_PULL_DOWN | GPIO_INPUT);
	gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 0);
#endif
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */
}

/* Any glados boards post version 2 should have ROP_LDO_EN stuffed. */
#define BOARD_MIN_ID_LOD_EN 2
/* Make the pmic re-sequence the power rails under these conditions. */
#define PMIC_RESET_FLAGS \
	(EC_RESET_FLAG_WATCHDOG | EC_RESET_FLAG_SOFT | EC_RESET_FLAG_HARD)
static void board_handle_reboot(void)
{
#if 0 /* MEC17xx EVB + SKL-RVP3 does not use chromebook PMIC design */
	int flags;
#endif
	CPRINTS("MEC HOOK_INIT - called board_handle_reboot");
	trace0(0, HOOK, 0, "HOOK_INIT - board_handle_reboot");

	if (system_jumped_to_this_image())
		return;

	if (system_get_board_version() < BOARD_MIN_ID_LOD_EN)
		return;

#if 0 /* TODO MCHP KBL hack not PMIC system */
	/* Interrogate current reset flags from previous reboot. */
	flags = system_get_reset_flags();

	if (!(flags & PMIC_RESET_FLAGS))
		return;

	/* Preserve AP off request. */
	if (flags & EC_RESET_FLAG_AP_OFF)
		chip_save_reset_flags(EC_RESET_FLAG_AP_OFF);

	ccprintf("Restarting system with PMIC.\n");
	/* Flush console */
	cflush();

	/* Bring down all rails but RTC rail (including EC power). */
	gpio_set_flags(GPIO_BATLOW_L_PMIC_LDO_EN, GPIO_OUT_HIGH);
	while (1)
		; /* wait here */
#else
	return;
#endif
}
DECLARE_HOOK(HOOK_INIT, board_handle_reboot, HOOK_PRIO_FIRST);

/* Indicate scheduler is alive by blinking an LED.
 * Test I2C by reading a smart battery and temperature sensor.
 * Smart battery 16 bit temperature is in units of 1/10 degree C.
 */
static void board_one_sec(void)
{
	trace0(0, BRD, 0, "HOOK_SECOND");
}
DECLARE_HOOK(HOOK_SECOND, board_one_sec, HOOK_PRIO_DEFAULT);

#ifdef HAS_TASK_MOTIONSENSE
/* Motion sensors */

static struct mutex g_base_mutex;
/* BMI160 private data */
static struct bmi_drv_data_t g_bmi160_data;

#ifdef CONFIG_ACCEL_KX022
static struct mutex g_lid_mutex;
/* KX022 private data */
static struct kionix_accel_data g_kx022_data;
#endif

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = CONFIG_SPI_ACCEL_PORT,
		.i2c_spi_addr_flags = SLAVE_MK_SPI_ADDR_FLAGS(
			CONFIG_SPI_ACCEL_PORT),
		.rot_standard_ref = NULL, /* Identity matrix. */
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
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
		.port = CONFIG_SPI_ACCEL_PORT,
		.i2c_spi_addr_flags = SLAVE_MK_SPI_ADDR_FLAGS(
			CONFIG_SPI_ACCEL_PORT),
		.default_range = 1000, /* dps */
		.rot_standard_ref = NULL, /* Identity Matrix. */
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
#ifdef CONFIG_ACCEL_KX022
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = NULL, /* Identity matrix. */
		.default_range = 2, /* g, enough for laptop. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
#endif /* #ifdef CONFIG_ACCEL_KX022 */
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void board_spi_enable(void)
{
	trace0(0, BRD, 0, "HOOK_CHIPSET_STARTUP - board_spi_enable");

	spi_enable(CONFIG_SPI_ACCEL_PORT, 1);

	/* Toggle SPI chip select to switch BMI160 from I2C mode
	 * to SPI mode
	 */
	/*
	gpio_set_level(GPIO_SPI0_CS0, 0);
	udelay(10);
	gpio_set_level(GPIO_SPI0_CS0, 1);
	*/
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_spi_enable,
	     MOTION_SENSE_HOOK_PRIO - 1);

static void board_spi_disable(void)
{
	trace0(0, BRD, 0, "HOOK_CHIPSET_SHUTDOWN - board_spi_disable");
	spi_enable(CONFIG_SPI_ACCEL_PORT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_spi_disable,
	     MOTION_SENSE_HOOK_PRIO + 1);
#endif /* defined(HAS_TASK_MOTIONSENSE) */

#ifdef MEC1701_EVB_TACH_TEST /* PWM/TACH test */
void tach0_isr(void)
{
	MCHP_INT_DISABLE(MCHP_TACH_GIRQ) = MCHP_TACH_GIRQ_BIT(0);
	MCHP_INT_SOURCE(MCHP_TACH_GIRQ) = MCHP_TACH_GIRQ_BIT(0);
}
DECLARE_IRQ(MCHP_IRQ_TACH_0, tach0_isr, 1);
#endif

void psensor_interrupt(enum gpio_signal signal)
{
	/* TODO: implement p-sensor interrupt function
	* when object close to p-sensor, trun on system to S0
	*/
}

void soc_hid_interrupt(enum gpio_signal signal)
{
	/* TODO: implement hid function */
}

void thermal_sensor_interrupt(enum gpio_signal signal)
{
	/* TODO: implement thermal sensor alert interrupt function */
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
			CPRINTS("Board version: %d", hx20_board_versions[i].version);
			return hx20_board_versions[i].version;
		}

	return BOARD_VERSION_UNKNOWN;
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
			ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3, ISL9241_CONTROL3_PSYS_GAIN |
			ISL9241_CONTROL3_ACLIM_RELOAD))
		goto init_fail;

	if (extpower_is_present() && battery_is_present())
		val |= ISL9241_CONTROL4_ACOK_PROCHOT;
	else if (battery_is_present())
		val |= ISL9241_CONTROL4_OTG_CURR_PROCHOT;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, val))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6800 | ISL9241_CONTROL1_SWITCH_FREQ;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, (battery_is_present() ? val |
		ISL9241_CONTROL1_SUPPLEMENTAL_SUPPORT_MODE : val)))
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
		if (extpower_is_present() && battery_is_present())
			val |= ISL9241_CONTROL4_ACOK_PROCHOT;
		else if (battery_is_present())
			val |= ISL9241_CONTROL4_OTG_CURR_PROCHOT;

		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL4, val)) {
			CPRINTS("update charger control4 fail!");
		}

		val = ISL9241_CONTROL1_PROCHOT_REF_6800 | ISL9241_CONTROL1_SWITCH_FREQ;
		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, (battery_is_present() ? val |
			ISL9241_CONTROL1_SUPPLEMENTAL_SUPPORT_MODE : val))) {
			CPRINTS("Update charger control1 fail");
		}

		pre_ac_state = extpower_is_present();
		pre_dc_state = battery_is_present();
	}
}
DECLARE_HOOK(HOOK_TICK, charger_update, HOOK_PRIO_DEFAULT);
#endif


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