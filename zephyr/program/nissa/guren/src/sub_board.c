/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guren sub-board hardware configuration */

#include "charge_state.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "guren_sub_board.h"
#include "hooks.h"
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

static uint8_t cached_usb_pd_port_count;

__override uint8_t board_get_usb_pd_port_count(void)
{
	__ASSERT(cached_usb_pd_port_count != 0,
		 "sub-board detection did not run before a port count request");
	if (cached_usb_pd_port_count == 0)
		LOG_WRN("USB PD Port count not initialized!");
	return cached_usb_pd_port_count;
}

test_export_static enum guren_sub_board_type guren_cached_sub_board =
	GUREN_SB_UNKNOWN;
/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum guren_sub_board_type guren_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (guren_cached_sub_board != GUREN_SB_UNKNOWN)
		return guren_cached_sub_board;

	guren_cached_sub_board = GUREN_SB_NONE; /* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return guren_cached_sub_board;
	}
	switch (val) {
	default:
		LOG_WRN("No sub-board defined");
		break;
	case FW_SUB_BOARD_1:
		guren_cached_sub_board = GUREN_SB_1C;
		LOG_INF("SB: USB type C");
		break;

	case FW_SUB_BOARD_2:
		guren_cached_sub_board = GUREN_SB_1A;
		LOG_INF("SB: USB type A");
		break;

	case FW_SUB_BOARD_3:
		guren_cached_sub_board = GUREN_SB_1C_1A;
		LOG_INF("SB: USB type C, USB type A");
		break;

	case FW_SUB_BOARD_4:
		guren_cached_sub_board = GUREN_SB_1C_LTE;
		LOG_INF("SB: USB type C, LTE");
		break;

	case FW_SUB_BOARD_5:
		guren_cached_sub_board = GUREN_SB_HDMI_LTE;
		LOG_INF("SB: HDMI, LTE");
		break;

	case FW_SUB_BOARD_6:
		guren_cached_sub_board = GUREN_SB_HDMI;
		LOG_INF("SB: HDMI");
		break;

	case FW_SUB_BOARD_7:
		guren_cached_sub_board = GUREN_SB_HDMI_1A;
		LOG_INF("SB: HDMI, USB type A");
		break;
	}
	return guren_cached_sub_board;
}

/*
 * Initialise the USB PD port count, which
 * depends on which sub-board is attached.
 */
test_export_static void board_usb_pd_count_init(void)
{
	switch (guren_get_sb_type()) {
	default:
		cached_usb_pd_port_count = 1;
		break;

	case GUREN_SB_1C_1A:
	case GUREN_SB_1C:
	case GUREN_SB_1C_LTE:
		cached_usb_pd_port_count = 2;
		break;
	}
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, board_usb_pd_count_init, HOOK_PRIO_INIT_I2C);

static void hdmi_hpd_interrupt(const struct device *device,
			       struct gpio_callback *callback,
			       gpio_port_pins_t pins)
{
	int state = gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_hpd_odl));

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd),
				state);
		LOG_INF("HDMI HPD changed state to %d", state);
	}
}

#if CONFIG_NISSA_BOARD_HAS_HDMI_SUPPORT
void nissa_configure_hdmi_rails(void)
{
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_rails_odl),
			      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
				      GPIO_PULL_UP | GPIO_ACTIVE_LOW);
}

#define I2C5_1_NODE DT_NODELABEL(i2c5_1)
#include <zephyr/drivers/pinctrl.h>
PINCTRL_DT_DEFINE(DT_NODELABEL(i2c5_1));
#endif

__override void nissa_configure_hdmi_power_gpios(void)
{
#if CONFIG_NISSA_BOARD_HAS_HDMI_SUPPORT
	const struct pinctrl_dev_config *pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i2c5_1));

	pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
#endif
	nissa_configure_hdmi_rails();
}

static void lte_power_handler(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data)
{
	/* Enable rails for S5 */
	const struct gpio_dt_spec *s5_rail =
		GPIO_DT_FROM_ALIAS(gpio_en_sub_s5_rails);
	switch (data.event) {
	case AP_POWER_PRE_INIT:
		LOG_DBG("Enabling LTE sub-board power rails");
		gpio_pin_set_dt(s5_rail, 1);
		break;
	case AP_POWER_HARD_OFF:
		LOG_DBG("Disabling LTE sub-board power rails");
		gpio_pin_set_dt(s5_rail, 0);
		break;
	default:
		LOG_ERR("Unhandled LTE power event %d", data.event);
		break;
	}
}

/**
 * Configure GPIOs (and other pin functions) that vary with present sub-board.
 *
 * The functions of some pins vary according to which sub-board is present
 * (indicated by CBI fw_config); this function configures them according to the
 * needs of the present sub-board.
 */
static void guren_subboard_config(void)
{
	enum guren_sub_board_type sb = guren_get_sb_type();
	static struct ap_power_ev_callback power_cb;

#if USB_PORT_ENABLE_COUNT > 1
	BUILD_ASSERT(USB_PORT_ENABLE_COUNT == 2,
		     "Guren assumes no more than 2 USB-A ports");
	/*
	 * USB-A port: current limit output is configured by default and unused
	 * if this port is not present. VBUS enable must be configured if
	 * needed and is controlled by the usba-port-enable-pins driver.
	 */
	if (sb == GUREN_SB_1C_1A || sb == GUREN_SB_1A ||
	    sb == GUREN_SB_HDMI_1A) {
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
		/* Turn off unused pins */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
				      GPIO_DISCONNECTED);
		/* Disable second USB-A port enable GPIO */
		usb_port_enable[1] = -1;
	}
#endif
	/*
	 * USB-C port: the default configuration has I2C on the I2C pins,
	 * but the interrupt line needs to be configured.
	 */
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	if (sb == GUREN_SB_1C_1A || sb == GUREN_SB_1C ||
	    sb == GUREN_SB_1C_LTE) {
		/* Configure interrupt input */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
				      GPIO_INPUT | GPIO_PULL_UP);
	} else {
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
	}
#endif

	if (sb == GUREN_SB_HDMI_LTE || sb == GUREN_SB_HDMI ||
	    sb == GUREN_SB_HDMI_1A) {
		/*
		 * HDMI: two outputs control power which must be configured to
		 * non-default settings, and HPD must be forwarded to the AP
		 * on another output pin.
		 */
		const struct gpio_dt_spec *hpd_gpio =
			GPIO_DT_FROM_ALIAS(gpio_hpd_odl);
		static struct gpio_callback hdmi_hpd_cb;
		int rv, irq_key;

		nissa_configure_hdmi_power_gpios();

		/*
		 * Configure HPD input from sub-board; it's inverted by a buffer
		 * on the sub-board.
		 */
		gpio_pin_configure_dt(hpd_gpio, GPIO_INPUT | GPIO_ACTIVE_LOW);
		/* Register interrupt handler for HPD changes */
		gpio_init_callback(&hdmi_hpd_cb, hdmi_hpd_interrupt,
				   BIT(hpd_gpio->pin));
		gpio_add_callback(hpd_gpio->port, &hdmi_hpd_cb);
		LOG_INF("SET HDMI HPD interrupt");
		rv = gpio_pin_interrupt_configure_dt(hpd_gpio,
						     GPIO_INT_EDGE_BOTH);
		__ASSERT(rv == 0,
			 "HPD interrupt configuration returned error %d", rv);
		/*
		 * Run the HPD handler once to ensure output is in sync.
		 * Lock interrupts to ensure that we don't cause desync if an
		 * HPD interrupt comes in between the internal read of the input
		 * and write to the output.
		 */
		irq_key = irq_lock();
		hdmi_hpd_interrupt(hpd_gpio->port, &hdmi_hpd_cb,
				   BIT(hpd_gpio->pin));
		irq_unlock(irq_key);
	}

	if ((sb == GUREN_SB_1C_LTE) || (sb == GUREN_SB_HDMI_LTE)) {
		/*
		 * LTE: Set up callbacks for enabling/disabling
		 * sub-board power on S5 state.
		 */
#if DT_NODE_EXISTS(DT_ALIAS(gpio_en_sub_s5_rails))
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_sub_s5_rails),
				      GPIO_OUTPUT_INACTIVE);
		/* Control LTE power when CPU entering or
		 * exiting S5 state.
		 */
		ap_power_ev_init_callback(&power_cb, lte_power_handler,
					  AP_POWER_HARD_OFF |
						  AP_POWER_PRE_INIT);
		ap_power_ev_add_callback(&power_cb);
#endif
	}
}
DECLARE_HOOK(HOOK_INIT, guren_subboard_config, HOOK_PRIO_POST_FIRST);

/*
 * Enable interrupts
 */
static void board_init(void)
{
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0));
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	if (board_get_usb_pd_port_count() == 2)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1));
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
