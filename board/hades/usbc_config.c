/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/tcpci_ppc.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/tcpm/anx7406.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>
#include <stdint.h>

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_TCPC,
			/* Circuit 1 (p1 = 0x70, p2 = 0x74) */
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = ANX7406_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7406_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_PPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &tcpci_ppc_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_PPC,
		.i2c_addr_flags = ANX7406_TCPC0_I2C_ADDR_FLAGS,
		.drv = &tcpci_ppc_drv,
	},
};

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const static struct ps8818_reg_val equalizer_default_table[] = {
	{
		.reg = PS8818_REG1_APTX1EQ_10G_LEVEL,
		.mask = PS8818_EQ_LEVEL_UP_MASK,
		.val = PS8818_EQ_LEVEL_UP_19DB,
	},
	{
		.reg = PS8818_REG1_APTX2EQ_10G_LEVEL,
		.mask = PS8818_EQ_LEVEL_UP_MASK,
		.val = PS8818_EQ_LEVEL_UP_19DB,
	},
	{
		.reg = PS8818_REG1_APTX1EQ_5G_LEVEL,
		.mask = PS8818_EQ_LEVEL_UP_MASK,
		.val = PS8818_EQ_LEVEL_UP_19DB,
	},
	{
		.reg = PS8818_REG1_APTX2EQ_5G_LEVEL,
		.mask = PS8818_EQ_LEVEL_UP_MASK,
		.val = PS8818_EQ_LEVEL_UP_19DB,
	},
	{
		.reg = PS8818_REG1_RX_PHY,
		.mask = PS8818_RX_INPUT_TERM_MASK,
		.val = PS8818_RX_INPUT_TERM_112_OHM,
	},
};

#define NUM_EQ_DEFAULT_ARRAY ARRAY_SIZE(equalizer_default_table)

int board_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int i;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		for (i = 0; i < NUM_EQ_DEFAULT_ARRAY; i++)
			rv |= ps8818_i2c_field_update8(
				me, PS8818_REG_PAGE1,
				equalizer_default_table[i].reg,
				equalizer_default_table[i].mask,
				equalizer_default_table[i].val);
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv |= ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					       PS8818_REG1_DPEQ_LEVEL,
					       PS8818_DPEQ_LEVEL_UP_MASK,
					       PS8818_DPEQ_LEVEL_UP_19DB);
	}

	return rv;
}

const static struct usb_mux_chain usbc2_ps8818 = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_USB_C1_TCPC,
			.i2c_addr_flags = PS8818_I2C_ADDR0_FLAGS,
			.driver = &ps8818_usb_retimer_driver,
			.board_set = &board_ps8818_mux_set,
		},
};

const struct pi3dpx1207_usb_control pi3dpx1207_controls[] = {
	[USBC_PORT_C0] = {
		.dp_enable_gpio = GPIO_USB_C0_IN_HPD,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3dpx1207_controls) == 1);

const struct usb_mux_chain usbc0_pi3dpx1207_usb_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_C0_TCPC,
			.i2c_addr_flags = PI3DPX1207_I2C_ADDR_FLAGS,
			.driver = &pi3dpx1207_usb_retimer,
		},
};

/* USBC mux configuration - Alder Lake includes internal mux */
const struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
		.next = &usbc0_pi3dpx1207_usb_retimer,
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
		.next = &usbc2_ps8818,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

#ifdef CONFIG_CHARGE_RAMP_SW

#define BC12_MIN_VOLTAGE 4400

/**
 * Return true if VBUS is too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	if (voltage == 0) {
		CPRINTS("%s: must be disconnected", __func__);
		return 1;
	}

	if (voltage < BC12_MIN_VOLTAGE) {
		CPRINTS("%s: port %d: vbus %d lower than %d", __func__, port,
			voltage, BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 0);

	/*
	 * delay for power-on to reset-off and min. assertion time
	 */
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);

	gpio_set_level(GPIO_USB_C0_TCPC_RST_ODL, 1);

	nct38xx_reset_notify(USBC_PORT_C0);

	/* wait for chips to come up */
	if (NCT3808_RESET_POST_DELAY_MS != 0)
		msleep(NCT3808_RESET_POST_DELAY_MS);
}

int board_tcpc_post_init(int port)
{
	int rv;

	if (port != USBC_PORT_C0)
		return EC_SUCCESS;

	/* Disable P2 on C0 */
	rv = i2c_write16(tcpc_config[port].i2c_info.port,
			 NCT38XX_I2C_ADDR2_1_FLAGS, TCPC_REG_ALERT_MASK,
			 TCPC_REG_ALERT_NONE);
	rv |= i2c_write16(tcpc_config[port].i2c_info.port,
			  NCT38XX_I2C_ADDR2_1_FLAGS, TCPC_REG_RX_DETECT,
			  TCPC_REG_RX_DETECT_NONE);
	rv |= i2c_write16(tcpc_config[port].i2c_info.port,
			  NCT38XX_I2C_ADDR2_1_FLAGS, TCPC_REG_COMMAND,
			  TCPC_REG_COMMAND_DISABLE_VBUS_DETECT);
	if (rv)
		CPRINTS("C0: Failed to disable P2 (0x%x)", rv);

	return rv;
}

static void board_tcpc_init(void)
{
	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	board_tcpc_post_init(USBC_PORT_C0);

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
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_0;

	if (gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;

	if (port == USBC_PORT_C1)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;

	return 0;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;
	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;
	default:
		break;
	}
}

static void ppc_handle_interrupt(int port)
{
	/*
	 * Ignore false positives (which may happen when we're already
	 * disconnected).
	 */
	if (!ppc_chips[port].drv->is_sourcing_vbus(port))
		return;

	/*
	 * If this is triggered by AOZ15333, it's either over current, short
	 * circuit, or over temperature. If this is triggered by AOZ13937, it's
	 * either over temperature, over voltage, or reverse current.
	 */
	ppc_prints("C%d: Vbus OC/OT/SC/OV/RC", port);
	pd_handle_overcurrent(port);
}

static atomic_t irq_pending; /* Bitmask of ports signaling an interrupt. */

static void ppc_irq_deferred(void)
{
	uint32_t pending = atomic_clear(&irq_pending);

	for (int i = 0; i < board_get_usb_pd_port_count(); i++)
		if (pending & BIT(i))
			ppc_handle_interrupt(i);
}
DECLARE_DEFERRED(ppc_irq_deferred);

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		atomic_or(&irq_pending, BIT(0));
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		atomic_or(&irq_pending, BIT(1));
		break;
	default:
		break;
	}
	hook_call_deferred(&ppc_irq_deferred_data, 0);
}

void retimer_interrupt(enum gpio_signal signal)
{
}

__override bool board_is_dts_port(int port)
{
	return port == USBC_PORT_C0;
}

__override bool board_is_tbt_usb4_port(int port)
{
	return false;
}

__override enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	if (!board_is_tbt_usb4_port(port))
		return TBT_SS_RES_0;

	return TBT_SS_TBT_GEN3;
}
