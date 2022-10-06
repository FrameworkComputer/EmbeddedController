/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nissa sub-board hardware configuration */

#include <ap_power/ap_power.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cros_board_info.h"
#include "driver/tcpm/tcpci.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc/usb_muxes.h"
#include "task.h"

#include "nissa_common.h"
#include "nissa_hdmi.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#if NISSA_BOARD_HAS_HDMI_SUPPORT
static void hdmi_power_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	/* Enable VCC on the HDMI port. */
	const struct gpio_dt_spec *s3_rail =
		GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl);
	/* Connect AP's DDC to sub-board (default is USB-C aux) */
	const struct gpio_dt_spec *ddc_select =
		GPIO_DT_FROM_NODELABEL(gpio_hdmi_sel);

	switch (data.event) {
	case AP_POWER_PRE_INIT:
		LOG_DBG("Connecting HDMI DDC to sub-board");
		gpio_pin_set_dt(ddc_select, 1);
		break;
	case AP_POWER_STARTUP:
		LOG_DBG("Enabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 1);
		break;
	case AP_POWER_SHUTDOWN:
		LOG_DBG("Disabling HDMI VCC");
		gpio_pin_set_dt(s3_rail, 0);
		break;
	case AP_POWER_HARD_OFF:
		LOG_DBG("Disconnecting HDMI sub-board DDC");
		gpio_pin_set_dt(ddc_select, 0);
		break;
	default:
		LOG_ERR("Unhandled HDMI power event %d", data.event);
		break;
	}
}

static void hdmi_hpd_interrupt(const struct device *device,
			       struct gpio_callback *callback,
			       gpio_port_pins_t pins)
{
	int state = gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_hpd_odl));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd), state);
	LOG_DBG("HDMI HPD changed state to %d", state);
}

void nissa_configure_hdmi_rails(void)
{
#if DT_NODE_EXISTS(GPIO_DT_FROM_ALIAS(gpio_en_rails_odl))
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_rails_odl),
			      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
				      GPIO_PULL_UP | GPIO_ACTIVE_LOW);
#endif
}

void nissa_configure_hdmi_vcc(void)
{
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_hdmi_en_odl),
			      GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN |
				      GPIO_ACTIVE_LOW);
}

__overridable void nissa_configure_hdmi_power_gpios(void)
{
	nissa_configure_hdmi_rails();
}

#ifdef CONFIG_SOC_IT8XXX2
/*
 * On it8xxx2, the below condition will break the EC to enter deep doze mode
 * (b:237717730):
 * Enhance i2c (GPE0/E7, GPH1/GPH2 or GPA4/GPA5) is enabled and its clock and
 * data pins aren't both at high level.
 *
 * Since HDMI+type A SKU doesn't use i2c4, disable it for better power number.
 */
#define I2C4_NODE DT_NODELABEL(i2c4)
#if DT_NODE_EXISTS(I2C4_NODE)
PINCTRL_DT_DEFINE(I2C4_NODE);

/* disable i2c4 alternate function  */
static void soc_it8xxx2_disable_i2c4_alt(void)
{
	const struct pinctrl_dev_config *pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(I2C4_NODE);

	pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
}
#endif /* DT_NODE_EXISTS(I2C4_NODE) */
#endif /* CONFIG_SOC_IT8XXX2 */
#endif /* NISSA_BOARD_HAS_HDMI_SUPPORT */

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
static void nereid_subboard_config(void)
{
	enum nissa_sub_board_type sb = nissa_get_sb_type();
	static struct ap_power_ev_callback power_cb;

	/*
	 * USB-A port: current limit output is configured by default and unused
	 * if this port is not present. VBUS enable must be configured if
	 * needed and is controlled by the usba-port-enable-pins driver.
	 */
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_HDMI_A) {
		/* Configure VBUS enable, default off */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
				      GPIO_OUTPUT_LOW);
	} else {
		/* Turn off unused pins */
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_sub_usb_a1_ilimit_sdp),
			GPIO_DISCONNECTED);
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus),
				      GPIO_DISCONNECTED);
		/* Disable second USB-A port enable GPIO */
		__ASSERT(USB_PORT_ENABLE_COUNT == 2,
			 "USB A port count != 2 (%d)", USB_PORT_ENABLE_COUNT);
		usb_port_enable[1] = -1;
	}
	/*
	 * USB-C port: the default configuration has I2C on the I2C pins,
	 * but the interrupt line needs to be configured.
	 */
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_C_LTE) {
		/* Configure interrupt input */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
				      GPIO_INPUT | GPIO_PULL_UP);
	} else {
		/* Port doesn't exist, doesn't need muxing */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_1_no_mux);
	}
#endif

	switch (sb) {
#if NISSA_BOARD_HAS_HDMI_SUPPORT
	case NISSA_SB_HDMI_A: {
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

#if CONFIG_SOC_IT8XXX2 && DT_NODE_EXISTS(I2C4_NODE)
		/* disable i2c4 alternate function for better power number */
		soc_it8xxx2_disable_i2c4_alt();
#endif

		/*
		 * Control HDMI power according to AP power state. Some events
		 * won't do anything if the corresponding pin isn't configured,
		 * but that's okay.
		 */
		ap_power_ev_init_callback(
			&power_cb, hdmi_power_handler,
			AP_POWER_PRE_INIT | AP_POWER_HARD_OFF |
				AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
		ap_power_ev_add_callback(&power_cb);

		/*
		 * Configure HPD input from sub-board; it's inverted by a buffer
		 * on the sub-board.
		 */
		gpio_pin_configure_dt(hpd_gpio, GPIO_INPUT | GPIO_ACTIVE_LOW);
		/* Register interrupt handler for HPD changes */
		gpio_init_callback(&hdmi_hpd_cb, hdmi_hpd_interrupt,
				   BIT(hpd_gpio->pin));
		gpio_add_callback(hpd_gpio->port, &hdmi_hpd_cb);
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
		break;
	}
#endif
	case NISSA_SB_C_LTE:
		/*
		 * LTE: Set up callbacks for enabling/disabling
		 * sub-board power on S5 state.
		 */
		gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_en_sub_s5_rails),
				      GPIO_OUTPUT_INACTIVE);
		/* Control LTE power when CPU entering or
		 * exiting S5 state.
		 */
		ap_power_ev_init_callback(&power_cb, lte_power_handler,
					  AP_POWER_HARD_OFF |
						  AP_POWER_PRE_INIT);
		ap_power_ev_add_callback(&power_cb);
		break;

	default:
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, nereid_subboard_config, HOOK_PRIO_POST_FIRST);

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

/* Trigger shutdown by enabling the Z-sleep circuit */
__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
}
