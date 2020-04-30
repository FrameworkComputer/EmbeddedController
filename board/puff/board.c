/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Puff board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "chipset.h"
#include "common.h"
#include "core/cortex-m/cpu.h"
#include "cros_board_info.h"
#include "driver/ina3221.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power/cometlake-discrete.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "thermistor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_TCPPC_INT_ODL)
		sn5s330_interrupt(0);
}

int ppc_get_alert_status(int port)
{
	return gpio_get_level(GPIO_USB_C0_TCPPC_INT_ODL) == 0;
}

static void tcpc_alert_event(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_TCPC_INT_ODL)
		schedule_deferred_pd_interrupt(0);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int level;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL)) {
		level = !!(tcpc_config[USB_PD_PORT_TCPC_0].flags &
			   TCPC_FLAGS_RESET_ACTIVE_HIGH);
		if (gpio_get_level(GPIO_USB_C0_TCPC_RST) != level)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	return status;
}

/* Called when the charge manager has switched to a new port. */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* Blink alert if insufficient power per system_can_boot_ap(). */
	int insufficient_power =
		(charge_ma * charge_mv) <
		(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON * 1000);
	led_alert(insufficient_power);
}

#include "port-sm.c"

static bool usbc_overcurrent;
/*
 * Update USB port power limits based on current state.
 *
 * Port power consumption is assumed to be negligible if not overcurrent,
 * and we have two knobs: the front port power limit, and the USB-C power limit.
 * Either one of the front ports may run at high power (one at a time) or we
 * can limit both to low power. On USB-C we can similarly limit the port power,
 * but there's only one port.
 */
void update_port_limits(void)
{
	struct port_states state = {
		.bitfield = (!gpio_get_level(GPIO_USB_A0_OC_ODL)
			     << PORTMASK_FRONT_A0) |
			    (!gpio_get_level(GPIO_USB_A1_OC_ODL)
			     << PORTMASK_FRONT_A1) |
			    (!gpio_get_level(GPIO_USB_A2_OC_ODL)
			     << PORTMASK_REAR_A0) |
			    (!gpio_get_level(GPIO_USB_A3_OC_ODL)
			     << PORTMASK_REAR_A1) |
			    (!gpio_get_level(GPIO_USB_A4_OC_ODL)
			     << PORTMASK_REAR_A2) |
			    (!gpio_get_level(GPIO_HDMI_CONN0_OC_ODL)
			     << PORTMASK_HDMI0) |
			    (!gpio_get_level(GPIO_HDMI_CONN1_OC_ODL)
			     << PORTMASK_HDMI1) |
			    ((ppc_is_sourcing_vbus(0) && usbc_overcurrent)
			     << PORTMASK_TYPEC),
		.front_a_limited = gpio_get_level(GPIO_USB_A_LOW_PWR_OD),
		/* Assume high-power; there's no way to poll this. */
		/*
		 * TODO(b/143190102) add a way to poll port power limit so we
		 * can detect when it can be increased again if the port is
		 * already active.
		 */
		.c_low_power = 0,
	};

	update_port_state(&state);

	ppc_set_vbus_source_current_limit(0,
		state.c_low_power ? TYPEC_RP_1A5 : TYPEC_RP_3A0);
	/* Output high limits power */
	gpio_set_level(GPIO_USB_A_LOW_PWR_OD, state.front_a_limited);
}
DECLARE_DEFERRED(update_port_limits);

static void port_ocp_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&update_port_limits_data, 0);
}

/******************************************************************************/
/*
 * Barrel jack power supply handling
 *
 * EN_PPVAR_BJ_ADP_L must default active to ensure we can power on when the
 * barrel jack is connected, and the USB-C port can bring the EC up fine in
 * dead-battery mode. Both the USB-C and barrel jack switches do reverse
 * protection, so we're safe to turn one on then the other off- but we should
 * only do that if the system is off since it might still brown out.
 */
#define ADP_DEBOUNCE_MS		1000  /* Debounce time for BJ plug/unplug */
/* Debounced connection state of the barrel jack */
static int8_t adp_connected = -1;
static void adp_connect_deferred(void)
{
	struct charge_port_info pi = { 0 };
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_L);

	/* Debounce */
	if (connected == adp_connected)
		return;
	if (connected) {
		pi.voltage = 19000;
		/*
		 * TODO(b:143975429) set current according to SKU.
		 * Different SKUs will ship with different power bricks
		 * that have varying power, though setting this to the
		 * maximum current available on any SKU may be okay
		 * (assume the included brick is sufficient to run the
		 * system at max power and over-reporting available
		 * power will have no effect).
		 */
		pi.current = 4740;
	}
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &pi);
	adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data, ADP_DEBOUNCE_MS * MSEC);
}

static void adp_state_init(void)
{
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, adp_state_init, HOOK_PRIO_CHARGE_MANAGER_INIT + 1);


#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN]        = { .channel = 5,
				.flags = PWM_CONFIG_OPEN_DRAIN,
				.freq = 25000},
	[PWM_CH_LED_RED]    = { .channel = 0,
				.flags = PWM_CONFIG_ACTIVE_LOW |
					 PWM_CONFIG_DSLEEP,
				.freq = 2000 },
	[PWM_CH_LED_GREEN]  = { .channel = 2,
				.flags = PWM_CONFIG_ACTIVE_LOW |
					 PWM_CONFIG_DSLEEP,
				.freq = 2000 },
};

/******************************************************************************/
/* USB-C TCPC Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		.flags = TCPC_FLAGS_RESET_ACTIVE_HIGH,
	},
};
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.usb_port = USB_PD_PORT_TCPC_0,
		.driver = &anx7447_usb_mux_driver,
		.hpd_update = &anx7447_tcpc_update_hpd_status,
	},
};

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"ina",     I2C_PORT_INA,     100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"ppc0",    I2C_PORT_PPC0,    100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"power",   I2C_PORT_POWER,   100, GPIO_I2C5_SCL, GPIO_I2C5_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct adc_t adc_channels[] = {
	[ADC_SNS_PP3300] = {
		/*
		 * 4700/5631 voltage divider: can take the value out of range
		 * for 32-bit signed integers, so truncate to 470/563 yielding
		 * <0.1% error and a maximum intermediate value of 1623457792,
		 * which comfortably fits in int32.
		 */
		.name = "SNS_PP3300",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT * 563,
		.factor_div = (ADC_READ_MAX + 1) * 470,
	},
	[ADC_SNS_PP1050] = {
		.name = "SNS_PP1050",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_VBUS] = {  /* 5/39 voltage divider */
		.name = "VBUS",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT * 39,
		.factor_div = (ADC_READ_MAX + 1) * 5,
	},
	[ADC_PPVAR_IMON] = {  /* 500 mV/A */
		.name = "PPVAR_IMON",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_SENSOR_1",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_SENSOR_2",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_PP3300] = {
		.name = "PP3300",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1,
	},
	[TEMP_SENSOR_PP5000] = {
		.name = "PP5000",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2400,
	.rpm_start = 2400,
	.rpm_max = 4000,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {NPCX_MFT_MODULE_2, TCKC_LFCLK, PWM_CH_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* Thermal control; drive fan based on temperature sensors. */
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = C_TO_K(75),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(55),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(55),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_PP3300] = thermal_a,
	[TEMP_SENSOR_PP5000] = thermal_a,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* Power sensors */
const struct ina3221_t ina3221[] = {
	{ I2C_PORT_INA, 0x40, { "PP3300_G", "PP5000_A", "PP3300_WLAN" } },
	{ I2C_PORT_INA, 0x42, { "PP3300_A", "PP3300_SSD", "PP3300_LAN" } },
	{ I2C_PORT_INA, 0x43, { NULL, "PP1200_U", "PP2500_DRAM" } }
};
const unsigned int ina3221_count = ARRAY_SIZE(ina3221);

static void board_init(void)
{
	uint8_t *memmap_batt_flags;

	/* Increase priority of C10 gate interrupts to minimize latency.
	 *
	 * We assume that GPIO_CPU_C10_GATE_L is on GPIO6.7, which is on
	 * the WKINTH_1 IRQ.
	 */
	const int c10_gpio_irq = NPCX_IRQ_WKINTH_1;
	const int c10_gpio_prio = 2;
	const uint32_t prio_shift = c10_gpio_irq % 4 * 8 + 5;

	CPU_NVIC_PRI(c10_gpio_irq / 4) =
		(CPU_NVIC_PRI(c10_gpio_irq / 4) &
		 ~(0x7 << prio_shift)) |
		(c10_gpio_prio << prio_shift);

	update_port_limits();
	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);

	/* Always claim AC is online, because we don't have a battery. */
	memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB-A port control */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USB_VBUS,
};

/* Power Delivery and charging functions */
static void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();
	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	/* Enable other overcurrent interrupts */
	gpio_enable_interrupt(GPIO_HDMI_CONN0_OC_ODL);
	gpio_enable_interrupt(GPIO_HDMI_CONN1_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A1_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A2_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A3_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A4_OC_ODL);

}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

int64_t get_time_dsw_pwrok(void)
{
	/* DSW_PWROK is turned on before EC was powered. */
	return -20 * MSEC;
}

void board_reset_pd_mcu(void)
{
	/* Maybe should only reset if we are powered off barreljack */
	int level = !!(tcpc_config[USB_PD_PORT_TCPC_0].flags &
		       TCPC_FLAGS_RESET_ACTIVE_HIGH);

	gpio_set_level(GPIO_USB_C0_TCPC_RST, level);
	msleep(BOARD_TCPC_C0_RESET_HOLD_DELAY);
	gpio_set_level(GPIO_USB_C0_TCPC_RST, !level);
	if (BOARD_TCPC_C0_RESET_POST_DELAY)
		msleep(BOARD_TCPC_C0_RESET_POST_DELAY);
}

int board_set_active_charge_port(int port)
{
	CPRINTS("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_active, bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_active = !gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L);
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_active != bj_requested)
			return EC_ERROR_INVAL;
	}

	CPRINTS("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
		/* TODO(b/143975429) need to touch the PD controller? */
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (gpio_get_level(GPIO_BJ_ADP_PRESENT_L))
			return EC_ERROR_INVAL;
		/* TODO(b/143975429) need to touch the PD controller? */
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Sanity check the port. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;
	usbc_overcurrent = is_overcurrented;
}

int extpower_is_present(void)
{
	return adp_connected;
}

static uint16_t board_version;

static void load_board_info(void)
{
	/*
	 * Load board info from CBI to control per-device configuration.
	 *
	 * If unset it's safe to treat the board as a proto, just C10 gating
	 * won't be enabled.
	 */
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS && val <= UINT16_MAX)
		board_version = val;
	CPRINTS("Board Version: 0x%04x", board_version);
}
DECLARE_HOOK(HOOK_INIT, load_board_info, HOOK_PRIO_INIT_I2C + 1);

int board_is_c10_gate_enabled(void)
{
	/*
	 * Puff proto drives EN_PP5000_HDMI from EN_S0_RAILS so we cannot gate
	 * core rails while in S0 because HDMI should remain powered.
	 * EN_PP5000_HDMI is a separate EC output on all other boards.
	 */
	return board_version != 0;
}

void board_enable_s0_rails(int enable)
{
	/* This output isn't connected on protos; safe to set anyway. */
	gpio_set_level(GPIO_EN_PP5000_HDMI, enable);
}

