/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific configuration */
#include "adc_chip.h"
#include "bb_retimer.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "cros_board_info.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/sn5s330.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tusb422.h"
#include "driver/temp_sensor/thermistor.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "icelake.h"
#include "keyboard_scan.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "usbc_ppc.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/******************************************************************************/
/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_PP3300_REGULATOR] = {
		.name = "TEMP_PP3300_REGULATOR",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH8,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_FAN] = {
		.name = "TEMP_FAN",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_ACOK_OD,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Increase from 50 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/* Other values should be the same as the default configuration. */
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

/******************************************************************************/
/* Charger Chip Configuration */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

/******************************************************************************/
/*
 * PWROK signal configuration, see the PWROK Generation Flow Diagram (Figure
 * 235) in the Tiger Lake Platform Design Guide for the list of potential
 * signals.
 *
 * Volteer uses this power sequence:
 *	GPIO_EN_PPVAR_VCCIN - Turns on the VCCIN rail. Also used as a delay to
 *		the VCCST_PWRGD input to the AP so this signal must be delayed
 *		5 ms to meet the tCPU00 timing requirement.
 *	GPIO_EC_PCH_SYS_PWROK - Asserts the SYS_PWROK input to the AP. Delayed
 *		a total of 50 ms after ALL_SYS_PWRGD input is asserted. See
 *		b/144478941 for full discussion.
 *
 * Volteer does not provide direct EC control for the VCCST_PWRGD and PCH_PWROK
 * signals. If your board adds these signals to the EC, copy this array
 * to your board.c file and modify as needed.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
		.delay_ms = 5,
	},
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
		.delay_ms = 50 - 5,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	/* No delays needed during S0 exit */
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
	},
	/* Turn off VCCIN last */
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);

/******************************************************************************/
/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_CHARGER] = {.name = "Charger",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_1_CHARGER},
	[TEMP_SENSOR_2_PP3300_REGULATOR] = {.name = "PP3300 Regulator",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_2_PP3300_REGULATOR},
	[TEMP_SENSOR_3_DDR_SOC] = {.name = "DDR and SOC",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_3_DDR_SOC},
	[TEMP_SENSOR_4_FAN] = {.name = "Fan",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_4_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* EC thermal management configuration */

/*
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(50),
};

/*
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 80c
 */
const static struct ec_thermal_config thermal_inductor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(55),
};


struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_CHARGER]			= thermal_inductor,
	[TEMP_SENSOR_2_PP3300_REGULATOR]	= thermal_inductor,
	[TEMP_SENSOR_3_DDR_SOC]			= thermal_cpu,
	[TEMP_SENSOR_4_FAN]			= thermal_cpu,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* USBC TCPC configuration */
struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = TUSB422_I2C_ADDR_FLAGS,
		},
		.drv = &tusb422_tcpm_drv,
		.usb23 = USBC_PORT_0_USB2_NUM | (USBC_PORT_0_USB3_NUM << 4),
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = TUSB422_I2C_ADDR_FLAGS,
		},
		.drv = &tusb422_tcpm_drv,
		.usb23 = USBC_PORT_1_USB2_NUM | (USBC_PORT_1_USB3_NUM << 4),
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

/******************************************************************************/
/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* USBC mux configuration - Tiger Lake includes internal mux */
struct usb_mux usbc1_usb4_db_retimer = {
	.usb_port = USBC_PORT_C1,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT_USB_1_MIX,
	.i2c_addr_flags = USBC_PORT_C1_BB_RETIMER_I2C_ADDR,
};
struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc1_usb4_db_retimer,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

struct bb_usb_control bb_controls[] = {
	[USBC_PORT_C0] = {
		/* USB-C port 0 doesn't have a retimer */
	},
	[USBC_PORT_C1] = {
		.shared_nvm = false,
		.usb_ls_en_gpio = GPIO_USB_C1_LS_EN,
		.retimer_rst_gpio = GPIO_USB_C1_RT_RST_ODL,
		.force_power_gpio = GPIO_USB_C1_RT_FORCE_PWR,
	},
};
BUILD_ASSERT(ARRAY_SIZE(bb_controls) == USBC_PORT_COUNT);

static void baseboard_tcpc_init(void)
{
	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

/******************************************************************************/
/* PPC support routines */
void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		sn5s330_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C1);
	default:
		break;
	}
}

/******************************************************************************/
/* TCPC support routines */
uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set
	 */
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	else
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	/* TODO: b/140572591 - check correct operation for Volteer */

	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = USBC_PORT_C0;
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		port = USBC_PORT_C1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
		break;

	default:
		break;
	}
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			     port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}


	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTFUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTSUSB("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
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

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Note that the level is inverted because the pin is active low. */
	switch (port) {
	case USBC_PORT_C0:
		gpio_set_level(GPIO_USB_C0_OC_ODL, !is_overcurrented);
		break;
	case USBC_PORT_C1:
		gpio_set_level(GPIO_USB_C1_OC_ODL, !is_overcurrented);
		break;
	}
}

static void baseboard_init(void)
{
	/* Enable monitoring of the PROCHOT input to the EC */
	gpio_enable_interrupt(GPIO_EC_PROCHOT_IN_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);

static uint8_t board_id;

uint8_t get_board_id(void)
{
	return board_id;
}

__overridable void board_cbi_init(void)
{
}

/*
 * Read CBI from i2c eeprom and initialize variables for board variants
 *
 * Example for configuring for a USB3 DB:
 *   ectool cbi set 6 2 4 10
 */
static void cbi_init(void)
{
	uint32_t cbi_val;

	/* Board ID */
	if (cbi_get_board_version(&cbi_val) != EC_SUCCESS ||
	    cbi_val > UINT8_MAX)
		CPRINTS("CBI: Read Board ID failed");
	else
		board_id = cbi_val;

	CPRINTS("Board ID: %d", board_id);

	/* FW config */
	init_fw_config();

	/* Allow the board project to make runtime changes based on CBI data */
	board_cbi_init();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_FIRST);
