/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/rt1739.h"
#include "emul/emul_rt1739.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(pd_got_frs_signal, int);

#define FFF_FAKES_LIST(FAKE) FAKE(pd_got_frs_signal)

#define RT1739_PORT 0
#define RT1739_NODE DT_NODELABEL(rt1739_emul)

extern int rt1739_get_flag(int port);

const struct emul *rt1739_emul = EMUL_DT_GET(RT1739_NODE);

static struct rt1739_set_reg_entry_t *
get_next_reg_set_entry(struct _snode **iter_node, uint8_t target_reg)
{
	struct rt1739_set_reg_entry_t *iter_entry = NULL;

	while (iter_node != NULL) {
		iter_entry = SYS_SLIST_CONTAINER(*iter_node, iter_entry, node);
		if (iter_entry->reg == target_reg) {
			return iter_entry;
		}
		*iter_node = (*iter_node)->next;
	}
	return NULL;
}

static void test_next_set_entry(struct _snode **iter_node,
				uint8_t expected_reg_address,
				uint8_t expected_val)
{
	struct rt1739_set_reg_entry_t *entry = NULL;

	*iter_node = (*iter_node)->next;
	zassert_not_null(*iter_node);

	entry = SYS_SLIST_CONTAINER(*iter_node, entry, node);
	zassert_equal(entry->reg, expected_reg_address);
	zassert_equal(entry->val, expected_val);
}

static void test_frs_enable_reg_settings(bool expected_enabled)
{
	uint8_t frs_ctrl1_val, int_mask5_val, int_mask4_val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_CC_FRS_CTRL1,
					&frs_ctrl1_val));
	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_INT_MASK5,
					&int_mask5_val));
	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_INT_MASK4,
					&int_mask4_val));

	if (expected_enabled) {
		zassert_true(frs_ctrl1_val & RT1739_FRS_RX_EN);
		zassert_false(int_mask5_val & RT1739_BC12_SNK_DONE_MASK);
		zassert_true(int_mask4_val & RT1739_FRS_RX_MASK);
	} else {
		zassert_false(frs_ctrl1_val & RT1739_FRS_RX_EN);
		zassert_true(int_mask5_val & RT1739_BC12_SNK_DONE_MASK);
		zassert_false(int_mask4_val & RT1739_FRS_RX_MASK);
	}
}

static void test_vconn_enable_reg_settings(bool expected_enabled)
{
	uint8_t val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL1,
					&val));

	if (expected_enabled) {
		zassert_true(val & RT1739_VCONN_EN);
	} else {
		zassert_false(val & RT1739_VCONN_EN);
	}
}

static void test_polarity_reg_settings(int expected_polarity)
{
	uint8_t val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL1,
					&val));

	if (expected_polarity) {
		zassert_true(val & RT1739_VCONN_ORIENT);
	} else {
		zassert_false(val & RT1739_VCONN_ORIENT);
	}
}

static void test_set_vbus_source_current_limit(enum tcpc_rp_value expected_rp)
{
	uint8_t val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VBUS_OC_SETTING,
					&val));

	switch (expected_rp) {
	case TYPEC_RP_3A0:
		zassert_equal(val & RT1739_LV_SRC_OCP_MASK,
			      RT1739_LV_SRC_OCP_SEL_3_3A);
		break;
	case TYPEC_RP_1A5:
		zassert_equal(val & RT1739_LV_SRC_OCP_MASK,
			      RT1739_LV_SRC_OCP_SEL_1_75A);
		break;
	default:
		zassert_equal(val & RT1739_LV_SRC_OCP_MASK,
			      RT1739_LV_SRC_OCP_SEL_1_25A);
		break;
	}
}

static void test_vbus_source_enable_reg_settings(bool expected_enabled)
{
	uint8_t val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul,
					RT1739_REG_VBUS_SWITCH_CTRL, &val));

	if (expected_enabled) {
		zassert_true(val & RT1739_LV_SRC_EN);
	} else {
		zassert_false(val & RT1739_LV_SRC_EN);
	}
}

static void rt1739_test_before(void *data)
{
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

static void test_vbus_sink_enable_reg_settings(bool expected_enabled)
{
	uint8_t val;

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul,
					RT1739_REG_VBUS_SWITCH_CTRL, &val));

	if (expected_enabled) {
		zassert_true(val & RT1739_HV_SNK_EN);
	} else {
		zassert_false(val & RT1739_HV_SNK_EN);
	}
}

ZTEST_SUITE(rt1739_ppc, drivers_predicate_pre_main, NULL, rt1739_test_before,
	    NULL, NULL);

ZTEST(rt1739_ppc, test_init_common_settings)
{
	uint8_t val;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_DEVICE_ID0,
			      RT1739_DEVICE_ID_ES4);

	rt1739_ppc_drv.init(RT1739_PORT);

	/* Check is frs is set to disabled */
	test_frs_enable_reg_settings(false);

	/* vconn is set to disabled */
	test_vconn_enable_reg_settings(false);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VBUS_DET_EN,
					&val));
	zassert_true(val & RT1739_VBUS_PRESENT_EN);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_SBU_CTRL_01,
					&val));
	zassert_equal(val & (RT1739_DM_SWEN | RT1739_DP_SWEN |
			     RT1739_SBUSW_MUX_SEL),
		      RT1739_DM_SWEN | RT1739_DP_SWEN);

	/* Check is VBUS OVP set to 23V */
	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VBUS_OV_SETTING,
					&val));
	zassert_equal(val, (RT1739_OVP_SEL_23_0V << RT1739_VBUS_OVP_SEL_SHIFT) |
				   (RT1739_OVP_SEL_23_0V
				    << RT1739_VIN_HV_OVP_SEL_SHIFT));

	/* Check is VBUS OCP set to 3.3A */
	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VBUS_OC_SETTING,
					&val));
	zassert_equal(val, RT1739_HV_SINK_OCP_SEL_3_3A |
				   RT1739_OCP_TIMEOUT_SEL_16MS |
				   RT1739_LV_SRC_OCP_SEL_1_75A);
}

ZTEST(rt1739_ppc, test_init_with_dead_battery)
{
	struct _snode *iter_node = NULL;
	struct rt1739_set_reg_entry_t *set_sys_ctrl = NULL;
	struct rt1739_set_reg_entry_t *set_vbus_switch_ctrl = NULL;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_SYS_CTRL,
			      RT1739_DEAD_BATTERY);
	rt1739_emul_reset_set_reg_history(rt1739_emul);

	rt1739_ppc_drv.init(RT1739_PORT);

	/*
	 * Check is dead battery boot settings in b/267412033#comment6 is
	 * applied.
	 */
	iter_node = rt1739_emul_get_reg_set_history_head(rt1739_emul);
	set_sys_ctrl = get_next_reg_set_entry(&iter_node, RT1739_REG_SYS_CTRL);
	zassert_not_null(set_sys_ctrl,
			 "No entry for setting  RT1739_REG_SYS_CTRL");
	zassert_equal(set_sys_ctrl->val,
		      RT1739_DEAD_BATTERY | RT1739_SHUTDOWN_OFF);

	set_vbus_switch_ctrl =
		get_next_reg_set_entry(&iter_node, RT1739_REG_VBUS_SWITCH_CTRL);
	zassert_not_null(set_vbus_switch_ctrl,
			 "No entry for setting RT1739_REG_VBUS_SWITCH_CTRL");
	zassert_true((set_vbus_switch_ctrl->val & RT1739_HV_SNK_EN),
		     "sink not enabled");

	set_sys_ctrl = get_next_reg_set_entry(&iter_node, RT1739_REG_SYS_CTRL);
	zassert_not_null(
		set_sys_ctrl,
		"No entry for setting  RT1739_REG_SYS_CTRL after enabling sink");
	zassert_equal(set_sys_ctrl->val, RT1739_OT_EN | RT1739_SHUTDOWN_OFF);
}

ZTEST(rt1739_ppc, test_init_not_dead_battery)
{
	struct _snode *iter_node = NULL;
	struct rt1739_set_reg_entry_t *set_vbus_switch_ctrl = NULL;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_VBUS_SWITCH_CTRL,
			      ~RT1739_HV_SNK_EN);
	rt1739_emul_reset_set_reg_history(rt1739_emul);

	rt1739_ppc_drv.init(RT1739_PORT);

	/*
	 * b/275294155: check is only vbus reset.
	 */
	iter_node = rt1739_emul_get_reg_set_history_head(rt1739_emul);
	set_vbus_switch_ctrl =
		get_next_reg_set_entry(&iter_node, RT1739_REG_VBUS_SWITCH_CTRL);
	zassert_not_null(set_vbus_switch_ctrl,
			 "No entry for setting RT1739_REG_VBUS_SWITCH_CTRL");
	zassert_equal(set_vbus_switch_ctrl->val, 0);
}

ZTEST(rt1739_ppc, test_es1_specific_init)
{
	uint8_t val;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_DEVICE_ID0,
			      RT1739_DEVICE_ID_ES1);

	rt1739_ppc_drv.init(RT1739_PORT);

	zassert_ok(
		rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_SYS_CTRL1, &val));
	zassert_true(val & RT1739_OSC640K_FORCE_EN);

	zassert_ok(
		rt1739_emul_peek_reg(rt1739_emul, RT1739_VBUS_FAULT_DIS, &val));
	zassert_equal(val, RT1739_OVP_DISVBUS_EN | RT1739_UVLO_DISVBUS_EN |
				   RT1739_SCP_DISVBUS_EN |
				   RT1739_OCPS_DISVBUS_EN);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL3,
					&val));
	zassert_true(val & RT1739_VCONN_CLIMIT_EN);
}

ZTEST(rt1739_ppc, test_es2_specific_init)
{
	uint8_t val;
	struct _snode *iter_node = NULL;
	struct rt1739_set_reg_entry_t *hidden_mode_F1 = NULL;
	struct rt1739_set_reg_entry_t *vbus_switch_ctrl = NULL;
	int64_t vbus_switch_previous_set_time;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_DEVICE_ID0,
			      RT1739_DEVICE_ID_ES2);
	rt1739_emul_reset_set_reg_history(rt1739_emul);

	rt1739_ppc_drv.init(RT1739_PORT);

	iter_node = rt1739_emul_get_reg_set_history_head(rt1739_emul);

	/* Enter hidden mode with correct sequence */
	hidden_mode_F1 = get_next_reg_set_entry(&iter_node, 0xF1);
	zassert_not_null(hidden_mode_F1);
	zassert_equal(hidden_mode_F1->val, 0x62);
	test_next_set_entry(&iter_node, 0xF0, 0x86);

	/* Check is next access is to disable SWENB */
	test_next_set_entry(&iter_node, 0xE0, 0x07);

	/* Next two accesses should be leaving the hidden mode */
	test_next_set_entry(&iter_node, 0xF1, 0x0);
	test_next_set_entry(&iter_node, 0xF0, 0x0);

	/* Next three accesses are VBUS to VIN_LV leakage remove setting */
	test_next_set_entry(&iter_node, RT1739_VBUS_FAULT_DIS, 0);
	test_next_set_entry(&iter_node, RT1739_REG_VBUS_CTRL1, 0);
	test_next_set_entry(&iter_node, RT1739_REG_VBUS_SWITCH_CTRL, 0);

	/*
	 * Check is accessing RT1739_REG_VBUS_SWITCH_CTRL is each time waiting
	 * more than 5 ms
	 */
	vbus_switch_ctrl =
		SYS_SLIST_CONTAINER(iter_node, vbus_switch_ctrl, node);
	vbus_switch_previous_set_time = vbus_switch_ctrl->access_time;
	test_next_set_entry(&iter_node, RT1739_REG_VBUS_SWITCH_CTRL,
			    RT1739_LV_SRC_EN);
	vbus_switch_ctrl =
		SYS_SLIST_CONTAINER(iter_node, vbus_switch_ctrl, node);
	zassert_true(vbus_switch_ctrl->access_time -
			     vbus_switch_previous_set_time >=
		     5);

	vbus_switch_previous_set_time = vbus_switch_ctrl->access_time;
	test_next_set_entry(&iter_node, RT1739_REG_VBUS_SWITCH_CTRL, 0);
	vbus_switch_ctrl =
		SYS_SLIST_CONTAINER(iter_node, vbus_switch_ctrl, node);
	zassert_true(vbus_switch_ctrl->access_time -
			     vbus_switch_previous_set_time >=
		     5);

	zassert_ok(
		rt1739_emul_peek_reg(rt1739_emul, RT1739_VBUS_FAULT_DIS, &val));
	zassert_equal(val, RT1739_OVP_DISVBUS_EN | RT1739_UVLO_DISVBUS_EN |
				   RT1739_RCP_DISVBUS_EN |
				   RT1739_SCP_DISVBUS_EN);

	zassert_ok(
		rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VBUS_CTRL1, &val));
	zassert_equal(val, RT1739_HVLV_SCP_EN | RT1739_HVLV_OCRC_EN);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL3,
					&val));
	zassert_true(val & RT1739_VCONN_CLIMIT_EN);
}

ZTEST(rt1739_ppc, test_es4_specific_init)
{
	uint8_t val;

	rt1739_emul_write_reg(rt1739_emul, RT1739_REG_DEVICE_ID0,
			      RT1739_DEVICE_ID_ES4);

	rt1739_ppc_drv.init(RT1739_PORT);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_LVHVSW_OV_CTRL,
					&val));
	zassert_false(val & RT1739_OT_SEL_LVL);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL4,
					&val));
	zassert_equal(val & RT1739_VCONN_OCP_SEL_MASK,
		      RT1739_VCONN_OCP_SEL_600MA);

	zassert_ok(rt1739_emul_peek_reg(rt1739_emul, RT1739_REG_VCONN_CTRL3,
					&val));
	zassert_false(val & RT1739_VCONN_CLIMIT_EN);
}

ZTEST(rt1739_ppc, test_set_vbus_source_current_limit)
{
	rt1739_ppc_drv.set_vbus_source_current_limit(RT1739_PORT, TYPEC_RP_3A0);
	test_set_vbus_source_current_limit(TYPEC_RP_3A0);

	rt1739_ppc_drv.set_vbus_source_current_limit(RT1739_PORT, TYPEC_RP_1A5);
	test_set_vbus_source_current_limit(TYPEC_RP_1A5);

	rt1739_ppc_drv.set_vbus_source_current_limit(RT1739_PORT, TYPEC_RP_USB);
	test_set_vbus_source_current_limit(TYPEC_RP_USB);
}

ZTEST(rt1739_ppc, test_is_sourcing_vbus)
{
	rt1739_ppc_drv.vbus_source_enable(RT1739_PORT, true);
	zassert_true(rt1739_ppc_drv.is_sourcing_vbus(RT1739_PORT));

	rt1739_ppc_drv.vbus_source_enable(RT1739_PORT, false);
	zassert_false(rt1739_ppc_drv.is_sourcing_vbus(RT1739_PORT));
}

ZTEST(rt1739_ppc, test_vbus_sink_enable)
{
	rt1739_ppc_drv.vbus_sink_enable(RT1739_PORT, true);
	test_vbus_sink_enable_reg_settings(true);

	rt1739_ppc_drv.vbus_sink_enable(RT1739_PORT, false);
	test_vbus_sink_enable_reg_settings(false);
}

ZTEST(rt1739_ppc, test_vbus_source_enable)
{
	rt1739_ppc_drv.vbus_source_enable(RT1739_PORT, true);
	test_vbus_source_enable_reg_settings(true);

	rt1739_ppc_drv.vbus_source_enable(RT1739_PORT, false);
	test_vbus_source_enable_reg_settings(false);
}

ZTEST(rt1739_ppc, test_is_vbus_present)
{
	zassert_ok(rt1739_emul_write_reg(rt1739_emul, RT1739_REG_INT_STS4,
					 RT1739_VBUS_PRESENT));
	zassert_true(rt1739_ppc_drv.is_vbus_present(RT1739_PORT));

	zassert_ok(rt1739_emul_write_reg(rt1739_emul, RT1739_REG_INT_STS4, 0));
	zassert_false(rt1739_ppc_drv.is_vbus_present(RT1739_PORT));
}

ZTEST(rt1739_ppc, test_set_polarity)
{
	rt1739_ppc_drv.set_polarity(RT1739_PORT, 0);
	test_polarity_reg_settings(0);

	rt1739_ppc_drv.set_polarity(RT1739_PORT, 1);
	test_polarity_reg_settings(1);
}

ZTEST(rt1939_ppc, test_vconn_settings)
{
	rt1739_ppc_drv.set_vconn(RT1739_PORT, true);
	test_vconn_enable_reg_settings(true);

	rt1739_ppc_drv.set_vconn(RT1739_PORT, false);
	test_vconn_enable_reg_settings(false);
}

ZTEST(rt1739_ppc, test_frs_settings)
{
	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, true);
	test_frs_enable_reg_settings(true);

	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, false);
	test_frs_enable_reg_settings(false);
}

ZTEST(rt1739_ppc, test_interrupt)
{
	/* test FRS invoked */
	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, true);
	zassert_equal(RT1739_FLAGS_FRS_ENABLED, rt1739_get_flag(RT1739_PORT));
	zassert_equal(0, pd_got_frs_signal_fake.call_count);
	rt1739_ppc_drv.interrupt(RT1739_PORT);
	zassert_equal(RT1739_FLAGS_FRS_ENABLED | RT1739_FLAGS_FRS_RX_RECV,
		      rt1739_get_flag(RT1739_PORT));
	zassert_equal(1, pd_got_frs_signal_fake.call_count);
	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, false);

	/* test FRS invoked multiple times */
	zassert_equal(0, rt1739_get_flag(RT1739_PORT));
	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, true);
	zassert_equal(RT1739_FLAGS_FRS_ENABLED, rt1739_get_flag(RT1739_PORT));

	rt1739_ppc_drv.interrupt(RT1739_PORT);
	zassert_equal(2, pd_got_frs_signal_fake.call_count);
	zassert_equal(RT1739_FLAGS_FRS_ENABLED | RT1739_FLAGS_FRS_RX_RECV,
		      rt1739_get_flag(RT1739_PORT));

	rt1739_ppc_drv.interrupt(RT1739_PORT);
	/* should not call pd_got_frs_signal when called */
	zassert_equal(2, pd_got_frs_signal_fake.call_count);
	rt1739_ppc_drv.set_frs_enable(RT1739_PORT, false);
	zassert_equal(0, rt1739_get_flag(RT1739_PORT));
}
