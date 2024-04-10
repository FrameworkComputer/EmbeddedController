/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS configuration */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
#else
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
#endif
	},
	[USB_PD_PORT_TCPC_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

/******************************************************************************/
/* USB-C MUX Configuration */

#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
static int ps8751_tune_mux(const struct usb_mux *me)
{
	/* Tune USB mux registers for casta's port 0 Rx measurement */
	mux_write(me, PS8XXX_REG_MUX_USB_C2SS_EQ, 0x40);
	return EC_SUCCESS;
}
#endif

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			.board_init = &ps8751_tune_mux,
#else
			.driver = &anx7447_usb_mux_driver,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
#endif
		},
	},
	[USB_PD_PORT_TCPC_1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_1,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	}
};

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = NX20P3483_ADDR2_FLAGS,
		.drv = &nx20p348x_drv,
	},
	[USB_PD_PORT_TCPC_1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NX20P3483_ADDR2_FLAGS,
		.drv = &nx20p348x_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/******************************************************************************/
/* Power Delivery and charing functions */

void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_MUX_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_MUX_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void variant_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_PD_C0_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_PD_C1_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_MUX_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_MUX_INT_ODL);
}
/* Called after the baseboard_tcpc_init (via +3) */
DECLARE_HOOK(HOOK_INIT, variant_tcpc_init, HOOK_PRIO_INIT_I2C + 3);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_MUX_INT_ODL)) {
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
		if (gpio_get_level(GPIO_USB_C0_PD_RST_ODL))
#else
		if (!gpio_is_implemented(GPIO_USB_C0_PD_RST) ||
		    !gpio_get_level(GPIO_USB_C0_PD_RST))
#endif
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_MUX_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
	/*
	 * C0: Assert reset to TCPC0 (PS8751) for required delay if we have a
	 * battery
	 */
	if (battery_is_present() == BP_YES) {
		/*
		 * TODO(crbug:846412): After refactor, ensure that battery has
		 * enough charge to last the reboot as well
		 */
		gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 0);
		crec_msleep(PS8XXX_RESET_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 1);
	}
#else
	/*
	 * C0: Assert reset to TCPC0 (ANX7447) for required delay (1ms) only if
	 * we have a battery
	 *
	 * Note: The TEST_R pin is not hooked up to a GPIO on all boards, so
	 * verify the name exists before setting it.  After the name is
	 * introduced for later board firmware, this pin will still be wired
	 * to USB2_OTG_ID on the proto boards, which should be set to open
	 * drain so it can't be driven high.
	 */
	if (gpio_is_implemented(GPIO_USB_C0_PD_RST) &&
	    battery_is_present() == BP_YES) {
		gpio_set_level(GPIO_USB_C0_PD_RST, 1);
		crec_msleep(ANX74XX_RESET_HOLD_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST, 0);
		crec_msleep(ANX74XX_RESET_FINISH_MS);
	}
#endif
	/*
	 * C1: Assert reset to TCPC1 (PS8751) for required delay (1ms) only if
	 * we have a battery, otherwise we may brown out the system.
	 */
	if (battery_is_present() == BP_YES) {
		/*
		 * TODO(crbug:846412): After refactor, ensure that battery has
		 * enough charge to last the reboot as well
		 */
		gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);
		crec_msleep(PS8XXX_RESET_DELAY_MS);
		gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
	} else {
		CPRINTS("Skipping C1 TCPC reset because no battery");
	}
}
