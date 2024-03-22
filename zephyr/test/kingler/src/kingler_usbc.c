/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "baseboard_usbc_config.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpm.h"
#include "ec_app_main.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "emul/tcpc/emul_tcpci.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "test/drivers/utils.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "variant_db_detection.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define LOG_LEVEL 0
LOG_MODULE_REGISTER(npcx_usbc);

extern void board_tcpc_init(void);
void ppc_interrupt(enum gpio_signal);
void ccd_interrupt(enum gpio_signal);
void bc12_interrupt(enum gpio_signal);

FAKE_VALUE_FUNC(bool, in_interrupt_context);
FAKE_VALUE_FUNC(bool, ps8743_field_update, const struct usb_mux *, uint8_t,
		uint8_t, uint8_t);
FAKE_VALUE_FUNC(enum corsola_db_type, corsola_get_db_type);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, rt1718s_get_adc, int, enum rt1718s_adc_channel, int *);
FAKE_VALUE_FUNC(int, tcpci_get_vbus_voltage_no_check, int, int *);
FAKE_VALUE_FUNC(uint8_t, board_get_adjusted_usb_pd_port_count);
FAKE_VOID_FUNC(bmi3xx_interrupt);
FAKE_VOID_FUNC(hdmi_hpd_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(nx20p348x_interrupt, int);
FAKE_VOID_FUNC(ps185_hdmi_hpd_mux_set);
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t);
FAKE_VOID_FUNC(usb_mux_hpd_update, int, mux_state_t);

#define FFF_FAKES_LIST(FAKE)                       \
	FAKE(bmi3xx_interrupt)                     \
	FAKE(board_get_adjusted_usb_pd_port_count) \
	FAKE(corsola_get_db_type)                  \
	FAKE(hdmi_hpd_interrupt)                   \
	FAKE(in_interrupt_context)                 \
	FAKE(nx20p348x_interrupt)                  \
	FAKE(ppc_vbus_sink_enable)                 \
	FAKE(ps185_hdmi_hpd_mux_set)               \
	FAKE(ps8743_field_update)                  \
	FAKE(rt1718s_get_adc)                      \
	FAKE(tcpci_get_vbus_voltage_no_check)      \
	FAKE(usb_charger_task_set_event)           \
	FAKE(usb_mux_hpd_update)

struct kingler_usbc_fixture {
	int place_holder;
};

static void *kingler_usbc_setup(void)
{
	static struct kingler_usbc_fixture f;

	return &f;
}

static void kingler_usbc_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

static void kingler_usbc_reset_rule_after(const struct ztest_unit_test *test,
					  void *data)
{
	for (int i = 0; i < 2; i++) {
		pd_power_supply_reset(i);
	}
}

ZTEST_RULE(kingler_usbc_reset_rule, kingler_usbc_reset_rule_before,
	   kingler_usbc_reset_rule_after);
ZTEST_SUITE(kingler_usbc, NULL, kingler_usbc_setup, NULL, NULL, NULL);

ZTEST_F(kingler_usbc, test_power_supply)
{
	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0));
	zassert_equal(1, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	pd_power_supply_reset(0);
	zassert_equal(0, ppc_is_sourcing_vbus(0));
	zassert_equal(0, ppc_is_sourcing_vbus(1));

	/* TODO: test C1 port after resolve the PPC emulator always accessing
	 * the same one with different index.
	 */
}

#define FAKE_INPUT_V 5000

int fake_rt1718s_get_adc(int p, enum rt1718s_adc_channel ch, int *v)
{
	*v = FAKE_INPUT_V;
	return 0;
}

int fake_tcpci_get_vbus_voltage_no_check(int p, int *v)
{
	*v = FAKE_INPUT_V;
	return 0;
}

ZTEST(kingler_usbc, test_get_vbus_voltage)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_TYPEC;
	zassert_equal(0, charge_manager_get_vbus_voltage(0));
	zassert_equal(0, charge_manager_get_vbus_voltage(1));

	tcpci_get_vbus_voltage_no_check_fake.custom_fake =
		fake_tcpci_get_vbus_voltage_no_check;
	rt1718s_get_adc_fake.custom_fake = fake_rt1718s_get_adc;

	zassert_equal(FAKE_INPUT_V, charge_manager_get_vbus_voltage(0));
	zassert_equal(FAKE_INPUT_V, charge_manager_get_vbus_voltage(1));
}

ZTEST(kingler_usbc, test_board_reset_pd_mcu)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	board_reset_pd_mcu();
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul, RT1718S_SYS_CTRL3,
					   &val),
		      EC_SUCCESS);
	/* ensure the mask is cleared */
	zassert_false(val & RT1718S_SWRESET_MASK);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_invalid)
{
	zassert_equal(EC_ERROR_INVAL, board_set_active_charge_port(5));
	zassert_equal(0, ppc_vbus_sink_enable_fake.call_count);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_none)
{
	board_get_adjusted_usb_pd_port_count_fake.return_val = 2;
	ppc_vbus_sink_enable_fake.return_val = 0;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(2, ppc_vbus_sink_enable_fake.call_count);
	/* C0 */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* C1 */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(kingler_usbc, test_board_set_active_charge_port_normal)
{
	board_get_adjusted_usb_pd_port_count_fake.return_val = 2;
	ppc_vbus_sink_enable_fake.return_val = 0;

	/* set charge port C0 */
	zassert_equal(EC_SUCCESS, board_set_active_charge_port(0));

	/* disable sink on other ports */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 0);

	/* enable sink on port 0 */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[1], 1);

	/* enable sink port on port 1 */
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[1], 1);

	/* set source at C0, and set charge port C0 */
	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0));
	zassert_equal(EC_ERROR_INVAL, board_set_active_charge_port(0));
}

ZTEST(kingler_usbc, test_board_vbus_source_enabled)
{
	pd_power_supply_reset(0);
	zassert_equal(board_vbus_source_enabled(0), 0);
	zassert_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	zassert_equal(board_vbus_source_enabled(0), 1);
}

ZTEST(kingler_usbc, test_bc12_interrupt)
{
	bc12_interrupt(0);
	zassert_equal(usb_charger_task_set_event_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg0_val, 0);
	zassert_equal(usb_charger_task_set_event_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
}

ZTEST(kingler_usbc, test_ppc_interrupt)
{
	zassert_equal(nx20p348x_interrupt_fake.call_count, 0);

	ppc_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_ppc_int_odl)));
	zassert_equal(nx20p348x_interrupt_fake.call_count, 1);
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 0);

	ppc_interrupt(GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl)));
	zassert_equal(nx20p348x_interrupt_fake.call_count, 2);
	zassert_equal(nx20p348x_interrupt_fake.arg0_val, 1);
}

ZTEST(kingler_usbc, test_board_tcpc_init)
{
	corsola_get_db_type_fake.return_val = CORSOLA_DB_NONE;
	board_tcpc_init();
	zassert_equal(usb_mux_hpd_update_fake.call_count,
		      CONFIG_USB_PD_PORT_MAX_COUNT);
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		zassert_equal(usb_mux_hpd_update_fake.arg0_history[i], i);
		zassert_equal(usb_mux_hpd_update_fake.arg1_history[i],
			      USB_PD_MUX_HPD_LVL_DEASSERTED |
				      USB_PD_MUX_HPD_IRQ_DEASSERTED);
	}
}

ZTEST(kingler_usbc, test_board_rt1718s_init)
{
	const struct emul *rt1718s_emul =
		EMUL_DT_GET(DT_NODELABEL(rt1718s_emul1));
	uint16_t val;

	board_rt1718s_init(1);

	/* check gpio1 config */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_GPIO1_VBUS_CTRL, &val),
		      EC_SUCCESS);
	zassert_equal(val & (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
			     RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO),
		      RT1718S_GPIO_VBUS_CTRL_ENA_SNK_VBUS_GPIO);

	/* check gpio2 config */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_GPIO2_VBUS_CTRL, &val),
		      EC_SUCCESS);
	zassert_equal(val & (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
			     RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO),
		      (RT1718S_GPIO_VBUS_CTRL_FRS_RX_VBUS |
		       RT1718S_GPIO_VBUS_CTRL_ENA_SRC_VBUS_GPIO));

	/* check bc12 src disabled */
	zassert_equal(rt1718s_emul_get_reg(rt1718s_emul,
					   RT1718S_RT2_BC12_SRC_FUNC, &val),
		      EC_SUCCESS);

	zassert_equal(val & RT1718S_RT2_BC12_SRC_FUNC_BC12_SRC_EN, 0);
}
