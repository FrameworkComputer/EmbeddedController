/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zoombini board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "console.h"
#include "compile_time_macros.h"
#include "driver/pmic_tps650x30.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "ec_commands.h"
#ifdef CONFIG_ESPI_VW_SIGNALS
#include "espi.h"
#endif /* defined(CONFIG_ESPI_VW_SIGNALS) */
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "system.h"
#include "switch.h"
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

#ifdef BOARD_MEOWTH
/*
 * Meowth shares the TCPC Alert# line with the TI SN5S330's interrupt line.
 * Therefore, we need to also check on that part.
 */
static void usb_c_interrupt(enum gpio_signal s)
{
	int port = (s == GPIO_USB_C0_PD_INT_L) ? 0 : 1;

	tcpc_alert_event(s);
	sn5s330_interrupt(port);
}
#endif /* defined(BOARD_MEOWTH) */

#ifdef BOARD_ZOOMBINI
static void ppc_interrupt(enum gpio_signal s)
{
	switch (s) {
	case GPIO_USB_C0_PPC_INT_L:
		sn5s330_interrupt(0);
		break;

	case GPIO_USB_C1_PPC_INT_L:
		sn5s330_interrupt(1);
		break;

	case GPIO_USB_C2_PPC_INT_L:
		sn5s330_interrupt(2);
		break;

	default:
		break;

	};
}
#endif /* defined(BOARD_ZOOMBINI) */

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
#ifdef BOARD_ZOOMBINI
	[ADC_TEMP_SENSOR_SOC] = {
		"SOC", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},

	[ADC_TEMP_SENSOR_CHARGER] = {
		"CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
#else /* defined(BOARD_MEOWTH) */
	[ADC_TEMP_SENSOR_CHARGER] = {
		"CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},

	[ADC_TEMP_SENSOR_SOC] = {
		"SOC", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},

	[ADC_TEMP_SENSOR_WIFI] = {
		"WIFI", NPCX_ADC_CH8, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
#endif /* defined(BOARD_ZOOMBINI) */
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
/* TODO(aaboagye): Add the additional Meowth LEDs */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_GREEN] = { 0, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_RED] =   { 2, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_KBLIGHT] =   { 3, 0, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED"},
#ifdef CONFIG_ESPI_VW_SIGNALS
	{VW_SLP_S3_L,	      POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{VW_SLP_S4_L,	      POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
#else
	{GPIO_PCH_SLP_S3_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
#endif /* defined(CONFIG_ESPI_VW_SIGNALS) */
	{GPIO_PCH_SLP_SUS_L,  POWER_SIGNAL_ACTIVE_HIGH, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD, POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,    POWER_SIGNAL_ACTIVE_HIGH, "PMIC_DPWROK"},
#ifdef BOARD_ZOOMBINI
	{GPIO_PP5000_PGOOD,   POWER_SIGNAL_ACTIVE_HIGH, "PP5000_A_PGOOD"},
#endif /* defined(BOARD_ZOOMBINI) */
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map. */
#ifdef BOARD_ZOOMBINI
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  100, GPIO_I2C0_SCL,  GPIO_I2C0_SDA},
	{"pmic",    I2C_PORT_PMIC,   400, GPIO_I2C3_SCL,  GPIO_I2C3_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_I2C7_SCL,  GPIO_I2C7_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_TCPC0_SCL,  GPIO_TCPC0_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_TCPC1_SCL,  GPIO_TCPC1_SDA},
	{"tcpc2",   I2C_PORT_TCPC2, 1000, GPIO_TCPC2_SCL,  GPIO_TCPC2_SDA},
};
#else
const struct i2c_port_t i2c_ports[] = {
	{"battery", I2C_PORT_BATTERY,  100, GPIO_I2C0_SCL,  GPIO_I2C0_SDA},
	{"charger", I2C_PORT_CHARGER,  100, GPIO_I2C4_SCL,  GPIO_I2C4_SDA},
	{"pmic",    I2C_PORT_PMIC,   400, GPIO_I2C3_SCL,  GPIO_I2C3_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_I2C7_SCL,  GPIO_I2C7_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_TCPC0_SCL,  GPIO_TCPC0_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_TCPC1_SCL,  GPIO_TCPC1_SDA},
};
#endif

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TODO(aaboagye): Add the other ports. 3 for Zoombini, 2 for Meowth */
const struct ppc_config_t ppc_chips[] = {
	{
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

#ifdef BOARD_ZOOMBINI
/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB_A_5V_EN,
};

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Extra delay when KSO2 is tied to Cr50. */
	.output_settle_us = 60,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
#endif /* defined(BOARD_ZOOMBINI) */

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},

	{
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},

#ifdef BOARD_ZOOMBINI
	{
		.i2c_host_port = I2C_PORT_TCPC2,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
#endif /* defined(BOARD_ZOOMBINI) */
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

#ifdef BOARD_ZOOMBINI
	{
		.port_addr = 2,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
#endif /* defined(BOARD_ZOOMBINI) */
};

static void board_chipset_resume(void)
{
	/* Enable display backlight. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
#ifdef BOARD_MEOWTH
	gpio_set_level(GPIO_EN_PP1800_U, 1);
#endif /* defined(BOARD_MEOWTH) */
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
#ifdef BOARD_MEOWTH
	gpio_set_level(GPIO_EN_PP1800_U, 0);
#endif /* defined(BOARD_MEOWTH  ) */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	/* Disable display backlight. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_init(void)
{
#ifdef BOARD_ZOOMBINI
	struct charge_port_info chg;
	int i;
#endif /* defined(BOARD_ZOOMBINI) */

#ifdef BOARD_ZOOMBINI
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_L);
	gpio_enable_interrupt(GPIO_USB_C2_PPC_INT_L);
#endif /* defined(BOARD_ZOOMBINI) */

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_L);
#ifdef BOARD_ZOOMBINI
	gpio_enable_interrupt(GPIO_USB_C2_PD_INT_L);

	/* Initialize VBUS suppliers. */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		if (tcpm_get_vbus_level(i)) {
			chg.voltage = 5000;
			chg.current = USB_CHARGER_MIN_CURR_MA;
		} else {
			chg.voltage = 0;
			chg.current = 0;
		}
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, i, &chg);
	}
#endif /* defined(BOARD_ZOOMBINI) */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port)
{
	/* Sanity check the port. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_COUNT))
		return;

	/* Note that the levels are inverted because the pin is active low. */
	switch (port) {
	case 0:
		gpio_set_level(GPIO_USB_C0_OC_L, 0);
		break;

	case 1:
		gpio_set_level(GPIO_USB_C1_OC_L, 0);
		break;

#ifdef BOARD_ZOOMBINI
	case 2:
		gpio_set_level(GPIO_USB_C2_OC_L, 0);
		break;
#endif /* defined(BOARD_ZOOMBINI) */

	default:
		return;
	};

	/* TODO(aaboagye): Write a PD log entry for the OC event. */
	CPRINTS("C%d: overcurrent!", port);
}

static void board_pmic_init(void)
{
	/* No need to re-initialize the PMIC on sysjumps. */
	if (system_jumped_to_this_image())
		return;

	/*
	 * The PMIC_EN has been de-asserted since gpio_pre_init.  Make sure
	 * it's de-asserted for at least 30ms.
	 *
	 * TODO(aaboagye): Characterize the discharge times for the power rails
	 * to see if we can shorten this delay.
	 */
	while (get_time().val < 30 * MSEC)
		;
	gpio_set_level(GPIO_PMIC_EN, 1);

	/*
	 * PGMASK1 : Mask VCCIO and 5V from Power Good Tree
	 * [7] : 1b MVCCIOPG is masked.
	 * [2] : 1b MV5APG is masked.
	 */
	if (i2c_write8(I2C_PORT_PMIC, PMIC_I2C_ADDR, TPS650X30_REG_PGMASK1,
		       (1 << 7) | (1 << 2)))
		cprints(CC_SYSTEM, "PMIC init failed!");
	else
		cprints(CC_SYSTEM, "PMIC init'd");
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_INIT_I2C+1);

void board_reset_pd_mcu(void)
{
	/* GPIO_USB_PD_RST_L resets all the TCPCs. */
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	msleep(10); /* TODO(aaboagye): Verify min hold time. */
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
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
			if (rv) {
				CPRINTS("Disabling p%d sink path failed.", i);
				return rv;
			}
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
	/*
	 * To protect the charge inductor, at voltages above 18V we should
	 * set the current limit to 2.7A.
	 */
	if (charge_mv > 18000)
		charge_ma = MIN(2700, charge_ma);

	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
#ifdef BOARD_MEOWTH
	int regval;

	/*
	 * For Meowth, the interrupt line is shared between the TCPC and PPC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
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
#else
	if (!gpio_get_level(GPIO_USB_C0_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_1;
	if (!gpio_get_level(GPIO_USB_C2_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_2;
#endif /* defined(BOARD_ZOOMBINI) */

	return status;
}

/* TODO(aaboagye): Remove if not needed later. */
static int command_tcpc_dump_reg(int argc, char **argv)
{
	int port;
	int regval;
	int reg;
	int rv;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = atoi(argv[1]);

	if (port < 0 || port > 2)
		return EC_ERROR_PARAM1;

	/* Dump the regs for the queried TCPC port. */
	regval = 0;

	cflush();
	ccprintf("TCPC %d reg dump:\n", port);

	for (reg = 0; reg <= 0xff; reg++) {
		regval = 0;
		ccprintf("[0x%02x] = ", reg);
		rv = tcpc_read(port, reg, &regval);
		if (!rv)
			ccprintf("0x%02x\n", regval);
		else
			ccprintf("ERR (%d)\n", rv);
		cflush();
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tcpcdump, command_tcpc_dump_reg, "<port>",
			"Dumps TCPCI regs 0-ff");
