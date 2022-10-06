/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/sys/util.h>

#include "charger.h"
#include "driver/charger/rt9490.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"

/*
 * This workaround and the board id checks only apply to krabby and early
 * tentacruel devices.
 * Newer project should have all of these fixed.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_KRABBY) ||
	     IS_ENABLED(CONFIG_BOARD_TENTACRUEL) || IS_ENABLED(CONFIG_TEST));

static void enter_hidden_mode(void)
{
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0xF1, 0x69);
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0xF2, 0x96);
}

/* b/194967754#comment5: work around for IBUS ADC unstable issue */
static void ibus_adc_workaround(void)
{
	if (system_get_board_version() != 0) {
		return;
	}

	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    RT9490_REG_ADC_CHANNEL0, RT9490_VSYS_ADC_DIS, MASK_SET);

	enter_hidden_mode();

	/* undocumented registers... */
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0x52, 0xC4);

	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    RT9490_REG_ADC_CHANNEL0, RT9490_VSYS_ADC_DIS, MASK_CLR);
}

/* b/214880220#comment44: lock i2c at 400khz */
static void i2c_speed_workaround(void)
{
	if (system_get_board_version() >= 3) {
		return;
	}

	enter_hidden_mode();
	/* Set to Auto mode, default run at 400kHz */
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0x71, 0x22);
	/* Manually select for 400kHz, valid only when 0x71[7] == 1 */
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0xF7, 0x14);
}

static void eoc_deglitch_workaround(void)
{
	if (system_get_board_version() != 1) {
		return;
	}

	/* set end-of-charge deglitch time to 2ms */
	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    RT9490_REG_ADD_CTRL0, RT9490_TD_EOC, MASK_CLR);
}

static void disable_safety_timer(void)
{
	if (system_get_board_version() >= 2) {
		return;
	}
	/* Disable charge timer */
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags,
		   RT9490_REG_SAFETY_TMR_CTRL,
		   RT9490_EN_TRICHG_TMR | RT9490_EN_PRECHG_TMR |
			   RT9490_EN_FASTCHG_TMR);
}

static void board_rt9490_workaround(void)
{
	ibus_adc_workaround();
	i2c_speed_workaround();
	eoc_deglitch_workaround();
	disable_safety_timer();
}
DECLARE_HOOK(HOOK_INIT, board_rt9490_workaround, HOOK_PRIO_DEFAULT);
