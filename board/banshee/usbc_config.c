/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdbool.h>

#include "cbi.h"
#include "charger.h"
#include "charge_ramp.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/syv682x_public.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "ioexpander.h"
#include "system.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usbc_config.h"
#include "usbc_ppc.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/* USBC TCPC configuration */
const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_C1_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_C1_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR2_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C2] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C2_C3_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C3] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C2_C3_TCPC,
			.addr_flags = NCT38XX_I2C_ADDR2_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);
/******************************************************************************/
/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.frs_en = IOEX_USB_C0_FRS_EN,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = SYV682X_ADDR2_FLAGS,
		.frs_en = IOEX_USB_C1_FRS_EN,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C2] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = SYV682X_ADDR1_FLAGS,
		.frs_en = IOEX_USB_C2_FRS_EN,
		.drv = &syv682x_drv,
	},
	[USBC_PORT_C3] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = SYV682X_ADDR3_FLAGS,
		.frs_en = IOEX_USB_C3_FRS_EN,
		.drv = &syv682x_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USBC mux configuration - Alder Lake includes internal mux */
static const struct usb_mux_chain usbc0_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
static const struct usb_mux_chain usbc1_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
static const struct usb_mux_chain usbc2_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C2,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
static const struct usb_mux_chain usbc3_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C3,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};

const struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C0_C1_MUX,
			.i2c_addr_flags = USBC_PORT_C0_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc0_tcss_usb_mux,
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C0_C1_MUX,
			.i2c_addr_flags = USBC_PORT_C1_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc1_tcss_usb_mux,
	},
	[USBC_PORT_C2] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C2,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C2_C3_MUX,
			.i2c_addr_flags = USBC_PORT_C2_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc2_tcss_usb_mux,
	},
	[USBC_PORT_C3] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C3,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_USB_C2_C3_MUX,
			.i2c_addr_flags = USBC_PORT_C3_BB_RETIMER_I2C_ADDR,
		},
		.next = &usbc3_tcss_usb_mux,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_1_FLAGS,
	},
	[USBC_PORT_C2] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_2_FLAGS,
	},
	[USBC_PORT_C3] = {
		.i2c_port = I2C_PORT_USB_PPC_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_0_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

/*
 * USB C0 and C2 uses burnside bridge chips and have their reset
 * controlled by their respective TCPC chips acting as GPIO expanders.
 *
 * ioex_init() is normally called before we take the TCPCs out of
 * reset, so we need to start in disabled mode, then explicitly
 * call ioex_init().
 */

struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C0_C1_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
	[IOEX_C1_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C0_C1_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR2_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
	[IOEX_C2_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C2_C3_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
	[IOEX_C3_NCT38XX] = {
		.i2c_host_port = I2C_PORT_USB_C2_C3_TCPC,
		.i2c_addr_flags = NCT38XX_I2C_ADDR2_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO(b/181508008): tune this threshold
 */

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

__override int bb_retimer_power_enable(const struct usb_mux *me, bool enable)
{
	enum ioex_signal rst_signal;

	if (me->usb_port == USBC_PORT_C0)
		rst_signal = IOEX_USB_C0_RT_RST_ODL;
	else if (me->usb_port == USBC_PORT_C1)
		rst_signal = IOEX_USB_C1_RT_RST_ODL;
	else if (me->usb_port == USBC_PORT_C2)
		rst_signal = IOEX_USB_C2_RT_RST_ODL;
	else if (me->usb_port == USBC_PORT_C3)
		rst_signal = IOEX_USB_C3_RT_RST_ODL;
	else
		return EC_ERROR_INVAL;
	/*
	 * We do not have a load switch for the burnside bridge chips,
	 * so we only need to sequence reset.
	 */

	if (enable) {
		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us.
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		ioex_set_level(rst_signal, 1);
		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		msleep(1);
	} else {
		ioex_set_level(rst_signal, 0);
		msleep(1);
	}
	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b/179648104): figure out correct timing
	 */
	gpio_set_level(GPIO_USB_C0_C1_TCPC_RST_ODL, 0);
	gpio_set_level(GPIO_USB_C2_C3_TCPC_RST_ODL, 0);
	/*
	 * delay for power-on to reset-off and min. assertion time
	 */
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_set_level(GPIO_USB_C0_C1_TCPC_RST_ODL, 1);
	gpio_set_level(GPIO_USB_C2_C3_TCPC_RST_ODL, 1);

	nct38xx_reset_notify(USBC_PORT_C0);
	nct38xx_reset_notify(USBC_PORT_C1);
	nct38xx_reset_notify(USBC_PORT_C2);
	nct38xx_reset_notify(USBC_PORT_C3);

	/* wait for chips to come up */
	if (NCT3808_RESET_POST_DELAY_MS != 0)
		msleep(NCT3808_RESET_POST_DELAY_MS);
}

static void board_tcpc_init(void)
{
	/* Don't reset TCPCs after initial reset */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/*
	 * These IO expander pins are implemented using the
	 * C0,C1,C2,C3 TCPC, so they must be set up after the TCPC has
	 * been taken out of reset.
	 */
	ioex_init(IOEX_C0_NCT38XX);
	ioex_init(IOEX_C1_NCT38XX);
	ioex_init(IOEX_C2_NCT38XX);
	ioex_init(IOEX_C3_NCT38XX);

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C2_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C3_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_C1_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C2_C3_TCPC_INT_ODL);

	/* Enable BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C2_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C3_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_CHIPSET);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_C1_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_0 | PD_STATUS_TCPC_ALERT_1;

	if (gpio_get_level(GPIO_USB_C2_C3_TCPC_INT_ODL) == 0)
		status |= PD_STATUS_TCPC_ALERT_2 | PD_STATUS_TCPC_ALERT_3;

	return status;
}

int ppc_get_alert_status(int port)
{
	if (port == USBC_PORT_C0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	else if (port == USBC_PORT_C1)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;
	else if (port == USBC_PORT_C2)
		return gpio_get_level(GPIO_USB_C2_PPC_INT_ODL) == 0;
	else if (port == USBC_PORT_C3)
		return gpio_get_level(GPIO_USB_C3_PPC_INT_ODL) == 0;
	return 0;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_C1_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C2_C3_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USBC_PORT_C2);
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
	case GPIO_USB_C2_BC12_INT_ODL:
		usb_charger_task_set_event(2, USB_CHG_EVENT_BC12);
		break;
	case GPIO_USB_C3_BC12_INT_ODL:
		usb_charger_task_set_event(3, USB_CHG_EVENT_BC12);
		break;
	default:
		break;
	}
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C1);
		break;
	case GPIO_USB_C2_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C2);
		break;
	case GPIO_USB_C3_PPC_INT_ODL:
		syv682x_interrupt(USBC_PORT_C3);
		break;
	default:
		break;
	}
}

void retimer_interrupt(enum gpio_signal signal)
{
	/*
	 * TODO(b/179513527): add USB-C support
	 */
}
