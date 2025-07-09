/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Krabby board-specific USB-C configuration */

#include "charge_manager.h"
#include "console.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/tusb1064.h"
#include "i2c.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"
#include "zephyr_adc.h"

#include <zephyr/sys/util_macro.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define ITE_CC_PARAMETER(i, _)                                    \
	{                                                         \
		.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT, \
		.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT \
	}

int tusb1064_mux_1_board_init(const struct usb_mux *me)
{
	int rv;

	rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
			TUSB1064_REG_DP1DP3EQ_SEL,
			TUSB1064_DP1EQ(TUSB1064_DP_EQ_RX_8_9_DB) |
				TUSB1064_DP3EQ(TUSB1064_DP_EQ_RX_5_4_DB));
	if (rv)
		return rv;

	/* Enable EQ_OVERRIDE so the gain registers are used */
	return i2c_update8(me->i2c_port, me->i2c_addr_flags,
			   TUSB1064_REG_GENERAL, REG_GENERAL_EQ_OVERRIDE,
			   MASK_SET);
}

#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = { LISTIFY(
			CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT, ITE_CC_PARAMETER,
			(, )) };

	return &cc_parameter[port];
}
#endif

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
	int is_valid_port =
		(port >= 0 && port < board_get_adjusted_usb_pd_port_count());
	/* adjust the actual port count when not the type-c db connected. */

	if (!is_valid_port && port != CHARGE_PORT_NONE) {
		return EC_ERROR_INVAL;
	}

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		for (i = 0; i < board_get_adjusted_usb_pd_port_count(); i++) {
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
	for (i = 0; i < board_get_adjusted_usb_pd_port_count(); i++) {
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

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0) {
		return ADC_VBUS_C0;
	}
	if (port == 1) {
		return ADC_VBUS_C1;
	}
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */
