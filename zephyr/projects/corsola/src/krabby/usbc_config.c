/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Krabby board-specific USB-C configuration */

#include "adc.h"
#include "baseboard_usbc_config.h"
#include "bc12/pi3usb9201_public.h"
#include "charge_manager.h"
#include "charger.h"
#include "console.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/ps8743.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "usb_mux/it5205_public.h"
#include "usbc_ppc.h"

#include "variant_db_detection.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* charger */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = 0,
		.drv = NULL,
	},
};

const struct pi3usb9201_config_t
		pi3usb9201_bc12_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	/* [0]: unused */
	[1] = {
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	}
};
/* PPC */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C0_PPC_FRSINFO,
	},
	{
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C1_FRS_EN,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

struct bc12_config bc12_ports[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{ .drv = NULL },
	{ .drv = &pi3usb9201_drv },
};

void bc12_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
}


static void board_sub_bc12_init(void)
{
/*
 * This function seems quite broken, so leave it out for now.
 */
#ifndef CONFIG_ZEPHYR
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC)
		/*
		 * The original code had:
		 *
		 * gpio_enable_interrupt(GPIO_USB_C1_BC12_CHARGER_INT_ODL);
		 *
		 * But this interrupt was defined out in gpio_map.h for
		 * this EC type.
		 */
	else
		/* If this is not a Type-C subboard, disable the task. */
		/*
		 * And this function is not implemented in zephyr
		 */
		task_disable_task(TASK_ID_USB_CHG_P1);
#endif
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_sub_bc12_init, HOOK_PRIO_INIT_I2C + 1);

static void board_usbc_init(void)
{
	/*
	 * Original code was:
	 *
	 * gpio_enable_interrupt(GPIO_USB_C0_PPC_BC12_INT_ODL);
	 *
	 * But this interrupt is defined out in gpio_map.h for
	 * CONFIG_SOC_IT8XXX2
	 */
#ifdef CONFIG_SOC_NPCX9M3F
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
#endif
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_DEFAULT-1);

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

void board_usb_mux_init(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		ps8743_tune_usb_eq(&usb_muxes[1],
				   PS8743_USB_EQ_TX_12_8_DB,
				   PS8743_USB_EQ_RX_12_8_DB);
		ps8743_write(&usb_muxes[1],
				   PS8743_REG_HS_DET_THRESHOLD,
				   PS8743_USB_HS_THRESH_NEG_10);
	}
}
DECLARE_HOOK(HOOK_INIT, board_usb_mux_init, HOOK_PRIO_INIT_I2C + 1);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C1_PPC_INT_ODL)
		syv682x_interrupt(1);
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PPC_BC12_INT_ODL) == 0;
	if (port == 1 && corsola_get_db_type() == CORSOLA_DB_TYPEC)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;

	return 0;
}

const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = {
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
		},
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
		},
	};

	return &cc_parameter[port];
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: check correct operation for Corsola */
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	return 0;
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

int board_set_active_charge_port(int port)
{
	int i;
	int is_valid_port = (port >= 0 && port < board_get_usb_pd_port_count());

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTS("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

const struct usb_mux usbc0_virtual_mux = {
	.usb_port = 0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

const struct usb_mux usbc1_virtual_mux = {
	.usb_port = 1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

static int board_ps8743_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int reg = 0;

	rv = ps8743_read(me, PS8743_REG_MODE, &reg);
	if (rv)
		return rv;

	/* Disable FLIP pin, enable I2C control. */
	reg |= PS8743_MODE_FLIP_REG_CONTROL;
	/* Disable CE_USB pin, enable I2C control. */
	reg |= PS8743_MODE_USB_REG_CONTROL;
	/* Disable CE_DP pin, enable I2C control. */
	reg |= PS8743_MODE_DP_REG_CONTROL;

	/*
	 * DP specific config
	 *
	 * Enable/Disable IN_HPD on the DB.
	 */
	gpio_set_level(GPIO_USB_C1_DP_IN_HPD,
		       mux_state & USB_PD_MUX_DP_ENABLED);

	return ps8743_write(me, PS8743_REG_MODE, reg);
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX0,
		.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
		.driver = &it5205_usb_mux_driver,
		.next_mux = &usbc0_virtual_mux,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX1,
		.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
		.driver = &ps8743_usb_mux_driver,
		.next_mux = &usbc1_virtual_mux,
		.board_set = &board_ps8743_mux_set,
	},
};

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0)
		return  ADC_VBUS_C0;
	if (port == 1)
		return  ADC_VBUS_C1;
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */
