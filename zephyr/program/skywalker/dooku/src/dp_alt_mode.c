/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skywalker_usbc, LOG_LEVEL_INF);

uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];
/*
 * 1 USB-C only:
 * leverage skywalker_dp_aention, remove aux selection
 * and DP rst-in-rst-serve policy
 * */
static void skywalker_dp_attention(int port, uint32_t vdo_dp_status)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(vdo_dp_status);
	int irq = PD_VDO_DPSTS_HPD_IRQ(vdo_dp_status);
	int cur_lvl =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l));

	if (irq && !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a sho low pulse from the high level.
		 */
		LOG_ERR("ERR:HPD:IRQ&LOW\n");
		return;
	}
	if (irq && cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port]) {
			k_usleep(svdm_hpd_deadline[port] - now);
		}
		/* generate IRQ_HPD pulse */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 0);
		/*
		 * b/171172053#comment14: since the HPD_DSTREAM_DEBOUNCE_IRQ is
		 * very sho (500us), we can use k_busy_wait for more stable
		 * pulse period.
		 */
		k_busy_wait(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l),
				lvl);
	}
	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
	return;
}
static void skywalker_set_unattached(int port)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 0);
}
static int skywalker_pdc_cb_init(void)
{
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_UNATTACH,
					       skywalker_set_unattached);
	pdc_power_mgmt_register_board_callback(PDC_BOARD_CB_DP_ATTENTION,
					       skywalker_dp_attention);
	return 0;
}
SYS_INIT(skywalker_pdc_cb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
