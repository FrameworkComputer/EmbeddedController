/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Screebo board-specific USB-C configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "hooks.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "usb_config.h"
#include "usb_pd.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/gpio_defines.h>

LOG_MODULE_REGISTER(screebo, LOG_LEVEL_INF);

uint32_t usb_db_type;
uint32_t usb_mb_type;

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	if (usb_db_type == FW_USB_DB_USB3) {
		if (tcpc_config[1].rst_gpio.port) {
			gpio_pin_set_dt(&tcpc_config[1].rst_gpio, 1);
			crec_msleep(PS8XXX_RESET_DELAY_MS);
			gpio_pin_set_dt(&tcpc_config[1].rst_gpio, 0);
			crec_msleep(PS8815_FW_INIT_DELAY_MS);
		}
	} else {
		reset_nct38xx_port(USBC_PORT_C1);
	}
}

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

	ret = cros_cbi_get_fw_config(FW_USB_MB, &usb_mb_type);
	if (ret != 0) {
		LOG_ERR("Failed to get FW_USB_MB from CBI");
		usb_mb_type = FW_USB_MB_UNKNOWN;
	}

	switch (usb_db_type) {
	case FW_USB_DB_USB3:
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rst_odl));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_int_odl));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(ps_usb_c1_rt_rst_odl),
				      (GPIO_ODR_HIGH | GPIO_ACTIVE_LOW));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_frs_en));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_3p3_sx_en));
		break;

	case FW_USB_DB_USB4_HB:
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(nct_usb_c1_rst_odl),
				      (GPIO_ODR_HIGH | GPIO_ACTIVE_LOW));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_int_odl),
				      GPIO_INPUT);
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_rst_r_odl));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_frs_en));
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_pwr_en),
				      GPIO_OUTPUT_LOW);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(hbr_usb_c1_rt_rst),
				      GPIO_OUTPUT);
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
		/* GPIOB1 */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_rt_3p3_sx_en));
		break;
	}

	if (usb_mb_type != FW_USB_MB_USB4_HB) {
		/* Just HBR will use these gpios */
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_rt_3p3_sx_en));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_rt_int_odl));
		gpio_unused(GPIO_DT_FROM_NODELABEL(ioex_usb_c0_rt_rst_ls_l));
	}
}
DECLARE_HOOK(HOOK_INIT, setup_runtime_gpios, HOOK_PRIO_FIRST);

static void setup_alt_db(void)
{
	if (usb_db_type == FW_USB_DB_USB3) {
		LOG_INF("USB DB: USB3 DB connected");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_ps8815_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_ps8815_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_nx20p_port1);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_alt_db, HOOK_PRIO_POST_I2C);

static void setup_mb_usb(void)
{
	if (usb_mb_type == FW_USB_MB_USB3) {
		LOG_INF("USB MB: C0 port is USB3");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_usb3_port0);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mb_usb, HOOK_PRIO_POST_I2C);

__override bool board_is_tbt_usb4_port(int port)
{
	/* Both C0 and C1 are USB4 port */
	if (usb_mb_type == FW_USB_MB_USB4_HB)
		return true;

	return false;
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	switch (usb_db_type) {
	case FW_USB_DB_USB3:
	case FW_USB_DB_USB4_HB:
		return 2;
	default:
		return 1;
	}
}

static void hbr_rst_runtime_config(void)
{
	int ret;
	uint32_t board_version;

	ret = cbi_get_board_version(&board_version);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI board version");
		return;
	}

	/* Only proto board use the ioex */
	if (board_version == 0) {
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_hbr_rst_l));
		gpio_unused(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_hbr_rst_l));
		bb_controls[USBC_PORT_C0].retimer_rst_gpio =
			GPIO_SIGNAL(DT_NODELABEL(ioex_usb_c0_rt_rst_ls_l));
		bb_controls[USBC_PORT_C1].retimer_rst_gpio =
			GPIO_SIGNAL(DT_NODELABEL(ioex_usb_c1_rt_rst_ls_l));
	} else {
		gpio_unused(GPIO_DT_FROM_NODELABEL(ioex_usb_c0_rt_rst_ls_l));
		gpio_unused(GPIO_DT_FROM_NODELABEL(ioex_usb_c1_rt_rst_ls_l));
	}
}
DECLARE_HOOK(HOOK_INIT, hbr_rst_runtime_config, HOOK_PRIO_POST_I2C);
