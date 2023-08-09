/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rex board-specific USB-C mux configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usb_pd.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/gpio_defines.h>

LOG_MODULE_DECLARE(rex, CONFIG_REX_LOG_LEVEL);

uint32_t usb_db_type;

static int gpio_unused(const struct gpio_dt_spec *spec)
{
	return gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT_PULL_UP);
}

static void setup_runtime_gpios(void)
{
	int ret;

	ret = cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type);
	if (ret != EC_SUCCESS) {
		LOG_INF("Failed to get FW_USB_DB from CBI");
		usb_db_type = FW_USB_DB_NOT_CONNECTED;
	}

	switch (usb_db_type) {
	case FW_USB_DB_USB4_HB:
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_pwr_en),
				      GPIO_ODR_LOW);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_int_odl),
				      GPIO_INPUT);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_rst_odl),
				      GPIO_OUTPUT_LOW);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(syv_usb_c1_frs_en),
				      GPIO_OUTPUT_LOW);
		break;

	default:
		/* GPIO37 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rst_odl));
		/* GPIO72 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_int_odl));
		/* GPIO74 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_rst_r_odl));
		/* GPIO83 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_frs_en));
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, setup_runtime_gpios, HOOK_PRIO_FIRST);

static void setup_usb_db(void)
{
	switch (usb_db_type) {
	case FW_USB_DB_NOT_CONNECTED:
		LOG_INF("USB DB: not connected");
		break;
	case FW_USB_DB_USB4_HB:
		LOG_INF("USB DB: Setting HBR mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_hbr_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1716_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1, ppc_syv_port1);
		break;
	default:
		LOG_INF("USB DB: No known USB DB found");
	}
}
DECLARE_HOOK(HOOK_INIT, setup_usb_db, HOOK_PRIO_POST_I2C);

__override uint8_t board_get_usb_pd_port_count(void)
{
	switch (usb_db_type) {
	case FW_USB_DB_USB4_HB:
		return 2;
	default:
		return 1;
	}
}
