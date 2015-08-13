/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Glados board-specific configuration */

#include "adc_chip.h"
#include "bd99992gw.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)

#define I2C_ADDR_BD99992 0x60

/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
#endif
}

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

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,    1, "RSMRST_N_PWRGD"},
	{GPIO_PCH_SLP_S0_L,      1, "SLP_S0_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,     1, "SLP_SUS_DEASSERTED"},
	{GPIO_PMIC_DPWROK,       1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, full ADC is equivalent to 33V. */
	[ADC_VBUS] = {"VBUS", 33000, 1024, 0, 1},
	/* Adapter current output or battery discharging current */
	[ADC_AMON_BMON] = {"AMON_BMON", 1, 1, 0, 3},
	/* System current consumption */
	[ADC_PSYS] = {"PSYS", 1, 1, 0, 4},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"pmic",     MEC1322_I2C0_0, 400,  GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"muxes",    MEC1322_I2C0_1, 400,  GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"pd_mcu",   MEC1322_I2C1,  1000,  GPIO_I2C1_SCL,   GPIO_I2C1_SDA},
	{"sensors",  MEC1322_I2C2,   400,  GPIO_I2C2_SCL,   GPIO_I2C2_SDA  },
	{"batt",     MEC1322_I2C3,   100,  GPIO_I2C3_SCL,   GPIO_I2C3_SDA  },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

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
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0xa8,
		.driver = &pi3usb30532_usb_mux_driver,
	},
	{
		.port_addr = 0x20,
		.driver = &ps8740_usb_mux_driver,
	}
};

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_PD_RST_L, 1);
}

const struct temp_sensor_t temp_sensors[] = {
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val, 0, 4},

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

/*
 * Thermal limits for each temp sensor.  All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id.  To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{{0, 0, 0}, 0, 0},	/* Battery */
	{{0, 0, 0}, 0, 0},	/* Ambient */
	{{0, 0, 0}, 0, 0},	/* Charger */
	{{0, 0, 0}, 0, 0},	/* DRAM */
	{{0, 0, 0}, 0, 0},	/* Wifi */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{ 0 },
	{ 0 },
};

static void board_pmic_init(void)
{
	/* No need to re-init PMIC since settings are sticky across sysjump */
	if (system_jumped_to_this_image())
		return;

	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x38, 0x7a);
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
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
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/*
		 * TODO: currently we only get VBUS knowledge when charge
		 * is enabled. so, when not charging, we need to enable
		 * both ports. but, this is dangerous if you have two
		 * chargers plugged in and you set charge override to -1
		 * then it will enable both sides!
		 */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 0);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 0);
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
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
}

/* Called on AP S5 -> S3 transition */
void board_chipset_startup(void)
{
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_PP1800_DX_AUDIO_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
void board_chipset_resume(void)
{
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_PP1800_DX_SENSOR_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Turn off LEDs in hibernate */
uint32_t board_get_gpio_hibernate_state(uint32_t port, uint32_t pin)
{
	int i;
	const uint32_t led_gpios[][2] = {
		GPIO_TO_PORT_MASK_PAIR(GPIO_CHARGE_LED_1),
		GPIO_TO_PORT_MASK_PAIR(GPIO_CHARGE_LED_2),
	};

	/* LED GPIOs should be driven low to turn off LEDs */
	for (i = 0; i < ARRAY_SIZE(led_gpios); ++i)
		if (led_gpios[i][0] == port && led_gpios[i][1] == pin)
			return GPIO_OUTPUT | GPIO_LOW;

	/* Other GPIOs should be put in a low-power state */
	return GPIO_INPUT | GPIO_PULL_UP;
}
