/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/rt1718s.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "test/drivers/test_state.h"
#include "test_common.h"

#include <zephyr/fff.h>
#include <zephyr/sys/slist.h>
#include <zephyr/ztest.h>

ZTEST(rt1718s_adc, test_adc_read)
{
	for (int ch = RT1718S_ADC_VBUS1; ch <= RT1718S_ADC_CH11; ++ch) {
		int val;

		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT_INT6,
				     RT1718S_RT_INT6_INT_ADC_DONE);
		/*
		 * Set raw adc reading to 10, the expect return value for
		 * rt1718s_get_adc is (10 * unit), where unit = 12.5mV for
		 * VBUS1, 33mA for VBUS_CURRENT, and 4mV for all other channels.
		 */
		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_ADC_CHX_VOL_L(ch),
				     10);
		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_ADC_CHX_VOL_H(ch),
				     0);

		zassert_ok(rt1718s_get_adc(tcpm_rt1718s_port, ch, &val));

		compare_reg_val_with_mask(rt1718s_emul, RT1718S_ADC_CTRL_01, 0,
					  0xFF);
		compare_reg_val_with_mask(rt1718s_emul, RT1718S_ADC_CTRL_02, 0,
					  0x0F);

		if (ch == RT1718S_ADC_VBUS1) {
			zassert_equal(val, 125, "incorrect VBUS1, got %d", val);
		} else if (ch == RT1718S_ADC_VBUS_CURRENT) {
			zassert_equal(val, 330,
				      "incorrect VBUS_CURRENT, got %d", val);
		} else {
			zassert_equal(
				val, 40,
				"incorrect value in adc channel %d, got %d", ch,
				val);
		}

		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_ADC_CHX_VOL_L(ch),
				     0);
	}
}

ZTEST(rt1718s_adc, test_adc_read_timeout)
{
	int val;
	struct rt1718s_emul_data *rt1718s_data = rt1718s_emul->data;
	sys_slist_t *reg_history = &rt1718s_data->set_private_reg_history;
	struct set_reg_entry_t *entry;
	bool adc_ch00_en_found = false;

	zassert_equal(rt1718s_get_adc(tcpm_rt1718s_port, RT1718S_ADC_VBUS1,
				      &val),
		      EC_ERROR_TIMEOUT);

	/* verify that ADC VBUS1 has enabled */
	SYS_SLIST_FOR_EACH_CONTAINER(reg_history, entry, node)
	{
		if (entry->reg == RT1718S_ADC_CTRL_01 &&
		    entry->val == BIT(RT1718S_ADC_VBUS1)) {
			adc_ch00_en_found = true;
		}
	}

	zassert_true(adc_ch00_en_found);

	compare_reg_val_with_mask(rt1718s_emul, RT1718S_ADC_CTRL_01, 0, 0xFF);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_ADC_CTRL_02, 0, 0x0F);
}

ZTEST_SUITE(rt1718s_adc, drivers_predicate_post_main, NULL, NULL,
	    rt1718s_clear_set_reg_history, NULL);
