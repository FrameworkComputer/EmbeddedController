/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "power.h"
#include "usb_charge.h"
#include "usb_pd.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);
FAKE_VALUE_FUNC(int, board_vbus_source_enabled, int);
FAKE_VOID_FUNC(button_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(switch_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(ppc_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bc12_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(ccd_interrupt, enum gpio_signal);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(uint8_t, get_dp_pin_mode, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);

#define FFF_FAKES_LIST(FAKE)               \
	FAKE(board_set_active_charge_port) \
	FAKE(board_vbus_source_enabled)    \
	FAKE(button_interrupt)             \
	FAKE(xhci_interrupt)               \
	FAKE(switch_interrupt)             \
	FAKE(ppc_interrupt)                \
	FAKE(bc12_interrupt)               \
	FAKE(x_ec_interrupt)               \
	FAKE(bmi3xx_interrupt)             \
	FAKE(ppc_is_sourcing_vbus)         \
	FAKE(ppc_vbus_source_enable)       \
	FAKE(ppc_vbus_sink_enable)         \
	FAKE(pd_set_vbus_discharge)        \
	FAKE(pd_send_host_event)           \
	FAKE(get_dp_pin_mode)              \
	FAKE(ccd_interrupt)

extern int active_aux_port;

static void corsola_reset(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Reset fakes */
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();

	active_aux_port = -1;

	/* reset default */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel), 1);

	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		usb_mux_set(i, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, 0);
		dp_flags[i] = 0;
		dp_status[i] = 0;
	}
}

ZTEST(corsola_usb_pd_policy, test_pd_power_supply_reset)
{
	const int port = 0;
	int count = 0;

	ppc_is_sourcing_vbus_fake.return_val = 1;
	pd_power_supply_reset(port);
	zassert_equal(ppc_is_sourcing_vbus_fake.arg0_history[count], port);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_history[count], port);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_history[count], 0);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_history[count], port);
	zassert_equal(pd_set_vbus_discharge_fake.arg1_history[count], 1);
	zassert_equal(pd_send_host_event_fake.arg0_history[count],
		      PD_EVENT_POWER_CHANGE);

	ppc_is_sourcing_vbus_fake.return_val = 0;
	pd_power_supply_reset(port);
	count++;
	zassert_equal(ppc_is_sourcing_vbus_fake.arg0_history[count], port);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_history[count], port);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_history[count], 0);
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_send_host_event_fake.arg0_history[count],
		      PD_EVENT_POWER_CHANGE);
}

ZTEST(corsola_usb_pd_policy, test_pd_set_power_supply_ready_success)
{
	const int port = 0;

	ppc_vbus_sink_enable_fake.return_val = 0;
	ppc_vbus_source_enable_fake.return_val = 0;

	zassert_ok(pd_set_power_supply_ready(port));

	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], port);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 0);

	zassert_equal(pd_set_vbus_discharge_fake.arg0_history[0], port);
	zassert_equal(pd_set_vbus_discharge_fake.arg1_history[0], 0);

	zassert_equal(ppc_vbus_source_enable_fake.arg0_history[0], port);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_history[0], 1);
	zassert_equal(pd_send_host_event_fake.arg0_history[0],
		      PD_EVENT_POWER_CHANGE);
}

ZTEST(corsola_usb_pd_policy, test_pd_set_power_supply_ready_fail1)
{
	const int port = 0;

	ppc_vbus_sink_enable_fake.return_val = 1;
	ppc_vbus_source_enable_fake.return_val = 0;

	zassert_ok(!pd_set_power_supply_ready(port));
}

ZTEST(corsola_usb_pd_policy, test_pd_set_power_supply_ready_fail2)
{
	const int port = 0;

	ppc_vbus_sink_enable_fake.return_val = 0;
	ppc_vbus_source_enable_fake.return_val = 1;

	zassert_ok(!pd_set_power_supply_ready(port));
}

ZTEST(corsola_usb_pd_policy, test_pd_check_vconn_swap)
{
	const int port = 0;
	/* suspend */
	power_set_state(POWER_S3);
	zassert_equal(pd_check_vconn_swap(port), true);

	/* s0 */
	power_set_state(POWER_S0);
	zassert_equal(pd_check_vconn_swap(port), true);

	/* softoff */
	power_set_state(POWER_S5);
	zassert_equal(pd_check_vconn_swap(port), false);

	/* hardoff */
	power_set_state(POWER_G3);
	zassert_equal(pd_check_vconn_swap(port), false);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_attention_dp_unmuxable)
{
	uint32_t payload[] = { 0x0,
			       VDO_DP_STATUS(/*irq*/ 1, /*lvl*/ 1, /*amode*/ 0,
					     /*usbc*/ 1, /* mf */ 1, /* en */ 1,
					     /* lp */ 0, /* conn */ 0x02) };
	const int port = 0;

	usb_mux_set(1, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);

	zassert_equal(0, svdm_dp_attention(port, payload));
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_attention_dp_err)
{
	uint32_t payload[] = { 0x0,
			       VDO_DP_STATUS(/*irq*/ 1, /*lvl*/ 0, /*amode*/ 0,
					     /*usbc*/ 1, /* mf */ 1, /* en */ 1,
					     /* lp */ 0, /* conn */ 0x02) };
	const int port = 0;

	usb_mux_set(1, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);

	zassert_equal(0, svdm_dp_attention(port, payload));
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_attention_dp_lvl_high)
{
	uint32_t payload[] = { 0x0,
			       VDO_DP_STATUS(/*irq*/ 1, /*lvl*/ 1, /*amode*/ 0,
					     /*usbc*/ 1, /* mf */ 1, /* en */ 1,
					     /* lp */ 0, /* conn */ 0x02) };
	const int port = 0;
	const struct gpio_dt_spec *aux_path =
		GPIO_DT_FROM_NODELABEL(dp_aux_path_sel);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_dp_hpd_odl), 0);
	zassert_equal(1, svdm_dp_attention(port, payload));
	zassert_equal(usb_mux_get(port), USB_PD_MUX_DOCK | USB_PD_MUX_HPD_LVL |
						 USB_PD_MUX_HPD_IRQ);
	zassert_equal(gpio_emul_output_get(aux_path->port, aux_path->pin),
		      port);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_attention_dp_lvl_low)
{
	uint32_t payload[] = { 0x0,
			       VDO_DP_STATUS(/*irq*/ 0, /*lvl*/ 0, /*amode*/ 0,
					     /*usbc*/ 1, /* mf */ 1, /* en */ 1,
					     /* lp */ 0, /* conn */ 0x02) };
	const int port = 0;
	const struct gpio_dt_spec *aux_path =
		GPIO_DT_FROM_NODELABEL(dp_aux_path_sel);

	zassert_equal(1, svdm_dp_attention(port, payload));
	zassert_equal(usb_mux_get(port), USB_PD_MUX_USB_ENABLED);
	zassert_equal(gpio_emul_output_get(aux_path->port, aux_path->pin), 1);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_config_pin_mod_none)
{
	uint32_t payload[] = { 0x0, 0x0 };
	const int port = 0;

	get_dp_pin_mode_fake.return_val = 0;
	zassert_equal(0, svdm_dp_config(port, payload));
	zassert_equal(0, payload[0]);
	zassert_equal(0, payload[1]);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_config_pin_mode)
{
	uint32_t payload[] = { 0x0, 0x0 };
	const int port = 0;
	const int pin_mode = MODE_DP_PIN_D | MODE_DP_PIN_E;
	const int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	get_dp_pin_mode_fake.return_val = MODE_DP_PIN_D | MODE_DP_PIN_E;
	zassert_equal(2, svdm_dp_config(port, payload));

	zassert_equal(VDO(USB_SID_DISPLAYPORT, 1,
			  CMD_DP_CONFIG | VDO_OPOS(opos)),
		      payload[0]);
	zassert_equal(VDO_DP_CFG(pin_mode, /* pin mode */
				 1, /* DPv1.3 signaling */
				 2) /* UFP connected */,
		      payload[1]);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_post_config_active_port)
{
	const int port = 0;

	svdm_set_hpd_gpio(port, 1);
	svdm_dp_post_config(port);

	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			zassert_true(dp_flags[i] & DP_FLAGS_DP_ON);
		} else {
			zassert_false(dp_flags[i] & DP_FLAGS_DP_ON);
		}
	}

	zassert_equal(active_aux_port, port);
	zassert_equal(usb_mux_get(port), USB_PD_MUX_DP_ENABLED |
						 USB_PD_MUX_HPD_LVL |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}

ZTEST(corsola_usb_pd_policy, test_svdm_dp_post_config_inactive_port)
{
	const int port = 0;

	svdm_set_hpd_gpio(1, 1);
	svdm_dp_post_config(port);

	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port) {
			zassert_true(dp_flags[i] & DP_FLAGS_DP_ON);
		} else {
			zassert_false(dp_flags[i] & DP_FLAGS_DP_ON);
		}
	}

	zassert_equal(active_aux_port, 1);
	zassert_equal(usb_mux_get(port), 0);
}
ZTEST_SUITE(corsola_usb_pd_policy, NULL, NULL, corsola_reset, corsola_reset,
	    NULL);
