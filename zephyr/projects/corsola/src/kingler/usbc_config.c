/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kingler board-specific USB-C configuration */

#include "charger.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/charger/isl923x_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/rt1718s.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/usb_mux/ps8743.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include "baseboard_usbc_config.h"
#include "variant_db_detection.h"

/* TODO(b/220196310): Create GPIO driver for RT17181S TCPC */
#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif
#include "gpio.h"


#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		/* Alert is active-low, open-drain */
		.flags = TCPC_FLAGS_ALERT_OD | TCPC_FLAGS_VBUS_MONITOR |
			 TCPC_FLAGS_CONTROL_FRS,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1,
			.addr_flags = RT1718S_I2C_ADDR2_FLAGS,
		},
		.drv = &rt1718s_tcpm_drv,
		/* Alert is active-low, open-drain */
		.flags = TCPC_FLAGS_ALERT_OD | TCPC_FLAGS_VBUS_MONITOR |
			 TCPC_FLAGS_CONTROL_FRS,
	}
};

struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = NX20P3483_ADDR2_FLAGS,
		.drv = &nx20p348x_drv
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = NX20P3483_ADDR2_FLAGS,
		.drv = &nx20p348x_drv
	}
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB Mux */

/* USB Mux C1 : board_init of PS8743 */
static int ps8743_tune_mux(const struct usb_mux *me)
{
	ps8743_tune_usb_eq(me,
			PS8743_USB_EQ_TX_3_6_DB,
			PS8743_USB_EQ_RX_16_0_DB);

	return EC_SUCCESS;
}

void board_usb_mux_init(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		/* Disable DCI function. This is not needed for ARM. */
		ps8743_field_update(&usb_muxes[1],
				   PS8743_REG_DCI_CONFIG_2,
				   PS8743_AUTO_DCI_MODE_MASK,
				   PS8743_AUTO_DCI_MODE_FORCE_USB);
	}
}
DECLARE_HOOK(HOOK_INIT, board_usb_mux_init, HOOK_PRIO_INIT_I2C + 1);

const struct usb_mux usbc0_virtual_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

const struct usb_mux usbc1_virtual_mux = {
	.usb_port = USBC_PORT_C1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &anx7447_usb_mux_driver,
		.hpd_update = &anx7447_tcpc_update_hpd_status,
		.next_mux = &usbc0_virtual_mux,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_USB_C1,
		.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
		.driver = &ps8743_usb_mux_driver,
		.next_mux = &usbc1_virtual_mux,
		.board_init = &ps8743_tune_mux,
	},
};

struct bc12_config bc12_ports[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.drv = &pi3usb9201_drv,
	},
	[USBC_PORT_C1] = {
		.drv = &rt1718s_bc12_drv,
	}
};

const struct pi3usb9201_config_t
		pi3usb9201_bc12_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = { /* unused */ }
};

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}

	/* Enable TCPC interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_tcpc));
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c1_tcpc));
	}

	/* Enable BC1.2 interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port) {
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_POST_I2C);

__override int board_rt1718s_init(int port)
{
	static bool gpio_initialized;

	if (!system_jumped_late() && !gpio_initialized) {
		/* set GPIO 1~3 as push pull, as output, output low. */
		rt1718s_gpio_set_flags(port, RT1718S_GPIO1, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO2, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO3, GPIO_OUT_LOW);
		gpio_initialized = true;
	}

	/* gpio1 low, gpio2 output high when receiving frs signal */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_GPIO1_VBUS_CTRL,
			RT1718S_GPIO1_VBUS_CTRL_FRS_RX_VBUS, 0));
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_GPIO2_VBUS_CTRL,
			RT1718S_GPIO2_VBUS_CTRL_FRS_RX_VBUS, 0xFF));

	/* Turn on SBU switch */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT2_SBU_CTRL_01,
				RT1718S_RT2_SBU_CTRL_01_SBU_VIEN |
				RT1718S_RT2_SBU_CTRL_01_SBU2_SWEN |
				RT1718S_RT2_SBU_CTRL_01_SBU1_SWEN,
				0xFF));
	/* Trigger GPIO 1/2 change when FRS signal received */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_FRS_CTRL3,
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1,
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1));
	/* Set FRS signal detect time to 46.875us */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_FRS_CTRL1,
			RT1718S_FRS_CTRL1_FRSWAPRX_MASK,
			0xFF));

	return EC_SUCCESS;
}

__override int board_rt1718s_set_frs_enable(int port, int enable)
{
	if (port == USBC_PORT_C1)
		/*
		 * Use set_flags (implemented by a single i2c write) instead
		 * of set_level (= i2c_update) to save one read operation in
		 * FRS path.
		 */
		rt1718s_gpio_set_flags(port, GPIO_EN_USB_C1_FRS,
				enable ? GPIO_OUT_HIGH : GPIO_OUT_LOW);
	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{

	CPRINTS("Resetting TCPCs...");
	/* reset C0 ANX3447 */
	/* Assert reset */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst), 1);
	msleep(1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst), 0);
	/* After TEST_R release, anx7447/3447 needs 2ms to finish eFuse
	 * loading.
	 */
	msleep(2);

	/* reset C1 RT1718s */
	rt1718s_sw_reset(USBC_PORT_C1);
}

/* Used by Vbus discharge common code with CONFIG_USB_PD_DISCHARGE */
int board_vbus_source_enabled(int port)
{
	return ppc_is_sourcing_vbus(port);
}

__override int board_rt1718s_set_snk_enable(int port, int enable)
{
	if (port == USBC_PORT_C1) {
		rt1718s_gpio_set_level(port, GPIO_EN_USB_C1_SINK, enable);
	}

	return EC_SUCCESS;
}

int board_set_active_charge_port(int port)
{
	int i;
	bool is_valid_port =
		(port >= 0 && port < board_get_usb_pd_port_count());

	if (!is_valid_port && port != CHARGE_PORT_NONE) {
		return EC_ERROR_INVAL;
	}

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0)) {
				CPRINTS("Disabling C%d as sink failed.", i);
			}
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
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			continue;
		}

		if (ppc_vbus_sink_enable(i, 0)) {
			CPRINTS("C%d: sink path disable failed.", i);
		}
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_int_odl))) {
		if (!gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst))) {
			status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (!gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_int_odl))) {
		return status |= PD_STATUS_TCPC_ALERT_1;
	}
	return status;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int port;

	switch (signal) {
	case GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_tcpc_int_odl)):
		port = 0;
		break;
	case GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c1_tcpc_int_odl)):
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_ppc_int_odl)):
		ppc_chips[0].drv->interrupt(0);
		break;
	case GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl)):
		ppc_chips[1].drv->interrupt(1);
		break;
	default:
		break;
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
}

__override int board_get_vbus_voltage(int port)
{
	int voltage = 0;
	int rv;

	switch (port) {
	case USBC_PORT_C0:
		rv = tcpc_config[USBC_PORT_C0].drv->get_vbus_voltage(port,
								     &voltage);
		if (rv)
			return 0;
		break;
	case USBC_PORT_C1:
		rt1718s_get_adc(port, RT1718S_ADC_VBUS1, &voltage);
		break;
	default:
		return 0;
	}
	return voltage;
}
