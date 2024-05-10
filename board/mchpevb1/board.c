/* Copyright 2017 The ChromiumOS Authors
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
#include "als.h"
#include "battery_smart.h"
#include "bd99992gw.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_opt3001.h"
#include "driver/tcpm/tcpci.h"
#include "espi.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "lpc_chip.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "spi_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* NOTE: MEC17xx EVB + SKL RVP3 does not use BD99992 PMIC.
 * RVP3 PMIC controlled by RVP3 logic.
 */
#define I2C_ADDR_BD99992_FLAGS 0x30

/*
 * Maxim DS1624 I2C temperature sensor used for testing I2C.
 * DS1624 contains one internal temperature sensor
 * and EEPROM. It has no external temperature inputs.
 */
#define DS1624_I2C_ADDR_FLAGS (0x48 | I2C_FLAG_BIG_ENDIAN)
#define DS1624_IDX_LOCAL 0
#define DS1624_READ_TEMP16 0xAA /* read 16-bit temperature */
#define DS1624_ACCESS_CFG 0xAC /* read/write 8-bit config */
#define DS1624_CMD_START 0xEE
#define DS1624_CMD_STOP 0x22

/*
 * static global and routine to return smart battery
 * temperature when we do not build with charger task.
 */
static int smart_batt_temp;
static int ds1624_temp;
static int sb_temp(int idx, int *temp_ptr);
static int ds1624_get_val(int idx, int *temp_ptr);
static void board_spi_enable(void);
static void board_spi_disable(void);

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
	smart_batt_temp = 0;
	ds1624_temp = 0;

#ifdef CONFIG_CHIPSET_DEBUG
	MCHP_EC_JTAG_EN = MCHP_JTAG_ENABLE + MCHP_JTAG_MODE_SWD_SWV;
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_MCHP_48MHZ_OUT)
	gpio_set_alternate_function(1, 0x10000, 2);
#endif
}
#endif /* #ifdef CONFIG_BOARD_PRE_INIT */

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

	trace2(0, BRD, 0, "ALL_SYS_PWRGD=%d SYS_RESET_L=%d", allsys_in,
	       allsys_out);

	/*
	 * Wait at least 10 ms between power signals going high
	 */
	if (allsys_in)
		crec_msleep(100);

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
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
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

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels
 * name, factor multiplier, factor divider, shift, channel
 */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, full ADC is equivalent to 30V. */
	[ADC_VBUS] = { "VBUS", 30000, 1024, 0, 1 },
	/* Adapter current output or battery discharging current */
	[ADC_AMON_BMON] = { "AMON_BMON", 25000, 3072, 0, 3 },
	/* System current consumption */
	[ADC_PSYS] = { "PSYS", 1, 1, 0, 4 },
	[ADC_CASE] = { "CASE", 1, 1, 0, 7 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/*
 * MCHP EVB connected to KBL RVP3
 */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "sensors",
	  .port = MCHP_I2C_PORT4,
	  .kbps = 100,
	  .scl = GPIO_SMB04_SCL,
	  .sda = GPIO_SMB04_SDA },
	{ .name = "batt",
	  .port = MCHP_I2C_PORT5,
	  .kbps = 100,
	  .scl = GPIO_SMB05_SCL,
	  .sda = GPIO_SMB05_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*
 * Map ports to controller.
 * Ports may map to the same controller.
 */
const uint16_t i2c_port_to_ctrl[I2C_PORT_COUNT] = {
	(MCHP_I2C_CTRL0 << 8) + MCHP_I2C_PORT4,
	(MCHP_I2C_CTRL1 << 8) + MCHP_I2C_PORT5
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
	{ I2C_PORT_TCPC, CONFIG_TCPC_I2C_BASE_ADDR_FLAGS, &tcpci_tcpm_drv },

	{ I2C_PORT_TCPC, CONFIG_TCPC_I2C_BASE_ADDR_FLAGS + 1, &tcpci_tcpm_drv },
};
#endif

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ QMSPI0_PORT, 4, GPIO_QMSPI_CS0 },
#if defined(CONFIG_SPI_ACCEL_PORT)
	{ GPSPI0_PORT, 2, GPIO_SPI0_CS0 },
#endif
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
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
	gpio_power_down_module(MODULE_SPI_CONTROLLER);
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
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);
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

struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = 0x10,
				.driver = &ps8740_usb_mux_driver,
			},
	}
};
#endif

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_PD_RST_L, 0);
	crec_usleep(100);
	gpio_set_level(GPIO_PD_RST_L, 1);
}

/*
 *
 */
static int therm_get_val(int idx, int *temp_ptr)
{
	if (temp_ptr != NULL) {
		*temp_ptr = adc_read_channel(idx);
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM2;
}

#ifdef CONFIG_TEMP_SENSOR
#if 0 /* Chromebook design uses ADC in BD99992GW PMIC */
const struct temp_sensor_t temp_sensors[] = {
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0, 4},

	/* These BD99992GW temp sensors are only readable in S0 */
	{"Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM0, 4},
	{"Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM1, 4},
	{"DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM2, 4},
	{"Wifi", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
		BD99992GW_ADC_CHANNEL_SYSTHERM3, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
#else /* mec1701_evb test I2C and EC ADC */
/*
 * battery charge_get_battery_temp requires charger task running.
 * OR can we call into driver/battery/smart.c
 * int sb_read(int cmd, int *param)
 * sb_read(SB_TEMPERATURE, &batt_new.temperature)
 * Issue is functions in this table return a value from a memory array.
 * There's a task or hook that is actually reading the temperature.
 * We could implement a one second hook to call sb_read() and fill in
 * a static global in this module.
 */
const struct temp_sensor_t temp_sensors[] = {
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, sb_temp, 0 },
	{ "Ambient", TEMP_SENSOR_TYPE_BOARD, ds1624_get_val, 0 },
	{ "Case", TEMP_SENSOR_TYPE_CASE, therm_get_val, (int)ADC_CASE },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
#endif
#endif

#ifdef CONFIG_ALS
/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{ "TI", opt3001_init, opt3001_read_lux, 5 },
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);
#endif

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{ "Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
	  30 * MSEC, 0 },
	{ "Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L, 30 * MSEC,
	  0 },
};

/* MCHP mec1701_evb connected to Intel SKL RVP3 with Kabylake
 * processor we do not control the PMIC on SKL.
 */
static void board_pmic_init(void)
{
	int rv, cfg;

	/* No need to re-init PMIC since settings are sticky across sysjump */
	if (system_jumped_late())
		return;

#if 0 /* BD99992GW PMIC on a real Chromebook */
	/* Set CSDECAYEN / VCCIO decays to 0V at assertion of SLP_S0# */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x30, 0x4a);

	/*
	 * Set V100ACNT / V1.00A Control Register:
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x37, 0x1a);

	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x38, 0x7a);

	/* VRMODECTRL - enable low-power mode for VCCIO and V0.85A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x3b, 0x18);
#else
	CPRINTS("HOOK_INIT - called board_pmic_init");
	trace0(0, HOOK, 0, "HOOK_INIT - call board_pmic_init");

	/* Config DS1624 temperature sensor for continuous conversion */
	cfg = 0x66;
	rv = i2c_read8(I2C_PORT_THERMAL, DS1624_I2C_ADDR_FLAGS,
		       DS1624_ACCESS_CFG, &cfg);
	trace2(0, BRD, 0, "Read DS1624 Config rv = %d  cfg = 0x%02X", rv, cfg);

	if ((rv == EC_SUCCESS) && (cfg & (1u << 0))) {
		/* one-shot mode switch to continuous */
		rv = i2c_write8(I2C_PORT_THERMAL, DS1624_I2C_ADDR_FLAGS,
				DS1624_ACCESS_CFG, 0);
		trace1(0, BRD, 0, "Write DS1624 Config to 0, rv = %d", rv);
		/* writes to config require 10ms until next I2C command */
		if (rv == EC_SUCCESS)
			udelay(10000);
	}

	/* Send start command */
	rv = i2c_write8(I2C_PORT_THERMAL, DS1624_I2C_ADDR_FLAGS,
			DS1624_CMD_START, 1);
	trace1(0, BRD, 0, "Send Start command to DS1624 rv = %d", rv);

	return;
#endif
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	CPRINTS("MEC1701 HOOK_INIT - called board_init");
	trace0(0, HOOK, 0, "HOOK_INIT - call board_init");

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
	/* Enable tablet mode interrupt for input device enable */
	gpio_enable_interrupt(GPIO_TABLET_MODE_L);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

	if (system_jumped_late() && chipset_in_state(CHIPSET_STATE_ON)) {
		trace0(0, BRD, 0, "board_init: S0 call board_spi_enable");
		board_spi_enable();
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	CPRINTS("MEC1701 HOOK_AC_CHANGE - called board_extpower");
	trace0(0, HOOK, 0, "HOOK_AC_CHANGET - call board_extpower");
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

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
		trace1(0, BOARD, 0, "Skip enable charge port %d", charge_port);
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
					     GPIO_USB_C1_CHARGE_EN_L,
			       1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_EN_L :
					     GPIO_USB_C0_CHARGE_EN_L,
			       0);
	}

	return EC_SUCCESS;
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
	if (!gpio_get_level(GPIO_TABLET_MODE_L))
		kb_enable = tp_enable = 0;
	/* Disable TP if chipset is off */
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		tp_enable = 0;

	keyboard_scan_enable(kb_enable, KB_SCAN_DISABLE_LID_ANGLE);
	gpio_set_level(GPIO_ENABLE_TOUCHPAD, tp_enable);
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_STARTUP - called board_chipset_startup");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_STARTUP - board_chipset_startup");
	gpio_set_level(GPIO_USB1_ENABLE, 1);
	gpio_set_level(GPIO_USB2_ENABLE, 1);
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_SHUTDOWN board_chipset_shutdown");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_SHUTDOWN board_chipset_shutdown");
	gpio_set_level(GPIO_USB1_ENABLE, 0);
	gpio_set_level(GPIO_USB2_ENABLE, 0);
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	CPRINTS("MEC1701_EVG HOOK_CHIPSET_RESUME");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_RESUME - board_chipset_resume");
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
#if 0 /* TODO not implemented in gpio.inc */
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 1);
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 1);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume,
	     MOTION_SENSE_HOOK_PRIO - 1);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	CPRINTS("MEC1701 HOOK_CHIPSET_SUSPEND - called board_chipset_resume");
	trace0(0, HOOK, 0, "HOOK_CHIPSET_SUSPEND - board_chipset_suspend");
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
#if 0 /* TODO not implemented in gpio.inc */
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 0);
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

void board_hibernate_late(void)
{
	/* put host chipset into reset */
	gpio_set_level(GPIO_SYS_RESET_L, 0);

	/* Turn off LEDs in hibernate */
	gpio_set_level(GPIO_CHARGE_LED_1, 0);
	gpio_set_level(GPIO_CHARGE_LED_2, 0);

	/*
	 * Set PD wake low so that it toggles high to generate a wake
	 * event once we leave hibernate.
	 */
	gpio_set_level(GPIO_USB_PD_WAKE, 0);

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

	if (system_jumped_late())
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

static int sb_temp(int idx, int *temp_ptr)
{
	if (idx != 0)
		return EC_ERROR_PARAM1;

	if (temp_ptr == NULL)
		return EC_ERROR_PARAM2;

	*temp_ptr = smart_batt_temp;

	return EC_SUCCESS;
}

static int ds1624_get_val(int idx, int *temp_ptr)
{
	if (idx != 0)
		return EC_ERROR_PARAM1;

	if (temp_ptr == NULL)
		return EC_ERROR_PARAM2;

	*temp_ptr = ds1624_temp;

	return EC_SUCCESS;
}

/* call smart battery code to get its temperature
 * output is in tenth degrees C
 */
static void sb_update(void)
{
	int rv __attribute__((unused));

	rv = sb_read(SB_TEMPERATURE, &smart_batt_temp);
	smart_batt_temp = smart_batt_temp / 10;

	trace12(0, BRD, 0, "sb_read temperature rv=%d  temp=%d K", rv,
		smart_batt_temp);
}

/*
 * Read temperature from Maxim DS1624 sensor. It only has internal sensor
 * and is configured for continuous reading mode by default.
 * DS1624 does not implement temperature limits or other features of
 * sensors like the TMP411.
 * Output format is 16-bit MSB first signed celcius temperature in units
 * of 0.0625 degree Celsius.
 * b[15]=sign bit
 * b[14]=2^6, b[13]=2^5, ..., b[8]=2^0
 * b[7]=1/2, b[6]=1/4, b[5]=1/8, b[4]=1/16
 * b[3:0]=0000b
 *
 */
static void ds1624_update(void)
{
	uint32_t d;
	int temp;
	int rv __attribute__((unused));

	rv = i2c_read16(I2C_PORT_THERMAL, DS1624_I2C_ADDR_FLAGS,
			DS1624_READ_TEMP16, &temp);

	d = (temp & 0x7FFF) >> 8;
	if ((uint32_t)temp & BIT(7))
		d++;

	if ((uint32_t)temp & BIT(15))
		d |= (1u << 31);

	ds1624_temp = (int32_t)d;

	trace3(0, BRD, 0, "ds1624_update: rv=%d raw temp = 0x%04X tempC = %d",
	       rv, temp, ds1624_temp);
}

/* Indicate scheduler is alive by blinking an LED.
 * Test I2C by reading a smart battery and temperature sensor.
 * Smart battery 16 bit temperature is in units of 1/10 degree C.
 */
static void board_one_sec(void)
{
	trace0(0, BRD, 0, "HOOK_SECOND");

	if (gpio_get_level(GPIO_CHARGE_LED_2))
		gpio_set_level(GPIO_CHARGE_LED_2, 0);
	else
		gpio_set_level(GPIO_CHARGE_LED_2, 1);

	sb_update();
	ds1624_update();
}
DECLARE_HOOK(HOOK_SECOND, board_one_sec, HOOK_PRIO_DEFAULT);

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
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(
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
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(
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

	spi_enable(&spi_devices[1], 1);

	/* Toggle SPI chip select to switch BMI160 from I2C mode
	 * to SPI mode
	 */
	gpio_set_level(GPIO_SPI0_CS0, 0);
	udelay(10);
	gpio_set_level(GPIO_SPI0_CS0, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_spi_enable,
	     MOTION_SENSE_HOOK_PRIO - 1);

static void board_spi_disable(void)
{
	trace0(0, BRD, 0, "HOOK_CHIPSET_SHUTDOWN - board_spi_disable");
	spi_enable(&spi_devices[1], 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_spi_disable,
	     MOTION_SENSE_HOOK_PRIO + 1);

#ifdef MEC1701_EVB_TACH_TEST /* PWM/TACH test */
static void tach0_isr(void)
{
	MCHP_INT_DISABLE(MCHP_TACH_GIRQ) = MCHP_TACH_GIRQ_BIT(0);
	MCHP_INT_SOURCE(MCHP_TACH_GIRQ) = MCHP_TACH_GIRQ_BIT(0);
}
DECLARE_IRQ(MCHP_IRQ_TACH_0, tach0_isr, 1);
#endif
