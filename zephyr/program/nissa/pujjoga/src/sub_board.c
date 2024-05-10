/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pujjoga sub-board hardware configuration */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "pujjoga_sub_board.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

test_export_static enum pujjoga_sub_board_type pujjoga_cached_sub_board =
	PUJJOGA_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum pujjoga_sub_board_type pujjoga_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (pujjoga_cached_sub_board != PUJJOGA_SB_UNKNOWN)
		return pujjoga_cached_sub_board;

	pujjoga_cached_sub_board = PUJJOGA_SB_NONE; /* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return pujjoga_cached_sub_board;
	}
	switch (val) {
	default:
		LOG_WRN("No sub-board defined");
		break;
	case FW_SUB_BOARD_1:
		pujjoga_cached_sub_board = PUJJOGA_SB_HDMI_A;
		LOG_INF("SB: HDMI, USB type A");
		break;
	}
	return pujjoga_cached_sub_board;
}

#if CONFIG_NISSA_BOARD_HAS_HDMI_SUPPORT
static void hdmi_power_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	/* Enable VCC on the HDMI port. */
	const struct gpio_dt_spec *s3_rail =
		GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl);

	switch (data.event) {
	case AP_POWER_STARTUP:
		LOG_DBG("Enabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 1);
		break;
	case AP_POWER_SHUTDOWN:
		LOG_DBG("Disabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 0);
		break;
	default:
		LOG_ERR("Unhandled HDMI power event %d", data.event);
		break;
	}
}

void nissa_configure_hdmi_vcc(void)
{
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl),
			      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
				      GPIO_ACTIVE_LOW);
}
#endif

/**
 * Configure GPIOs (and other pin functions) that vary with present sub-board.
 *
 * The functions of some pins vary according to which sub-board is present
 * (indicated by CBI fw_config); this function configures them according to the
 * needs of the present sub-board.
 */
static void pujjoga_subboard_config(void)
{
	enum pujjoga_sub_board_type sb = pujjoga_get_sb_type();
#if CONFIG_NISSA_BOARD_HAS_HDMI_SUPPORT
	static struct ap_power_ev_callback power_cb;
#endif

	BUILD_ASSERT(USB_PORT_ENABLE_COUNT == 1,
		     "Pujjoga assumes no more than 1 USB-A ports");
	/*
	 * USB-A port: current limit output is configured by default and unused
	 * if this port is not present. VBUS enable must be configured if
	 * needed and is controlled by the usba-port-enable-pins driver.
	 */
	if (sb == PUJJOGA_SB_HDMI_A) {
		/*
		 * Configure VBUS enable, retaining current value.
		 * SB_NONE indicates missing fw_config; it's safe to enable VBUS
		 * control in this case since all that will happen is we turn
		 * off power to LTE, and it's useful to allow USB-A to work in
		 * such a configuration.
		 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
				      GPIO_OUTPUT);
	} else {
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
				      GPIO_DISCONNECTED);
	}

#if CONFIG_NISSA_BOARD_HAS_HDMI_SUPPORT
	/*
	 * Control HDMI power according to AP power state. Some events
	 * won't do anything if the corresponding pin isn't configured,
	 * but that's okay.
	 */
	ap_power_ev_init_callback(&power_cb, hdmi_power_handler,
				  AP_POWER_PRE_INIT | AP_POWER_HARD_OFF |
					  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&power_cb);
#endif
}
DECLARE_HOOK(HOOK_INIT, pujjoga_subboard_config, HOOK_PRIO_POST_FIRST);
