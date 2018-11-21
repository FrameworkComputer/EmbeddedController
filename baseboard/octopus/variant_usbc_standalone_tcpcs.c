/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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
#include "tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

#define USB_PD_PORT_TCPC_0	0
#define USB_PD_PORT_TCPC_1	1

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW
#else
		.i2c_slave_addr = AN7447_TCPC0_I2C_ADDR,
		.drv = &anx7447_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
#endif
	},
	[USB_PD_PORT_TCPC_1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* USB-C MUX Configuration */

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
#if defined(VARIANT_OCTOPUS_TCPC_0_PS8751)
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
#else
		.driver = &anx7447_usb_mux_driver,
		.hpd_update = &anx7447_tcpc_update_hpd_status,
#endif
	},
	[USB_PD_PORT_TCPC_1] = {
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = NX20P3483_ADDR2,
		.drv = &nx20p348x_drv,
	},
	[USB_PD_PORT_TCPC_1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr = NX20P3483_ADDR2,
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
/* Called after the baseboard_tcpc_init (via +2) */
DECLARE_HOOK(HOOK_INIT, variant_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

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
		msleep(PS8XXX_RESET_DELAY_MS);
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
		msleep(ANX74XX_RESET_HOLD_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST, 0);
		msleep(ANX74XX_RESET_FINISH_MS);
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
		msleep(PS8XXX_RESET_DELAY_MS);
		gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
	} else {
		CPRINTS("Skipping C1 TCPC reset because no battery");
	}
}

#define PS8751_DEBUG_ADDR	0x12
#define PS8751_GPIO_ENABLE	0x44
#define PS8751_GPIO_LVL		0x45
#define PS8751_GPIO3_VAL	(1 << 3)

static void set_ps8751_gpio3(int enable)
{
	int rv, reg;

	/*
	 * Ensure that we don't put the TCPC back to sleep while we are
	 * accessing debug registers.
	 */
	pd_prevent_low_power_mode(USB_PD_PORT_TCPC_1, 1);

	/* Enable debug page access */
	rv = tcpc_write(USB_PD_PORT_TCPC_1, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			0x30);
	if (rv)
		goto error;

	/* Enable GPIO3 (bit3) output by setting to bit3 to 1 */
	rv = i2c_read8(I2C_PORT_TCPC1, PS8751_DEBUG_ADDR, PS8751_GPIO_ENABLE,
		       &reg);
	if (rv)
		goto error;

	if (!(reg & PS8751_GPIO3_VAL)) {
		reg |= PS8751_GPIO3_VAL;

		rv = i2c_write8(I2C_PORT_TCPC1, PS8751_DEBUG_ADDR,
				PS8751_GPIO_ENABLE, reg);
		if (rv)
			goto error;
	}

	/* Set level for GPIO3, which controls the re-driver power */
	rv = i2c_read8(I2C_PORT_TCPC1, PS8751_DEBUG_ADDR, PS8751_GPIO_LVL,
		       &reg);
	if (rv)
		goto error;

	if (!!(reg & PS8751_GPIO3_VAL) != !!enable) {
		if (enable)
			reg |= PS8751_GPIO3_VAL;
		else
			reg &= ~PS8751_GPIO3_VAL;

		rv = i2c_write8(I2C_PORT_TCPC1, PS8751_DEBUG_ADDR,
				PS8751_GPIO_LVL, reg);
	}
error:
	if (rv)
		CPRINTS("C1: Could not set re-driver power to %d", enable);

	/* Disable debug page access and allow LPM again*/
	tcpc_write(USB_PD_PORT_TCPC_1, PS8XXX_REG_I2C_DEBUGGING_ENABLE, 0x31);
	pd_prevent_low_power_mode(USB_PD_PORT_TCPC_1, 0);
}

/*
 * Most boards do not stuff the re-driver. We always toggle GPIO3 on the PS8751
 * since it is benign if the re-driver isn't there.
 */
static void board_enable_a1_redriver(void)
{
	set_ps8751_gpio3(1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_enable_a1_redriver, HOOK_PRIO_DEFAULT);


static void board_disable_a1_redriver(void)
{
	set_ps8751_gpio3(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_disable_a1_redriver,
	     HOOK_PRIO_DEFAULT);
