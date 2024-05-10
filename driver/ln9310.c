/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LION Semiconductor LN-9310 switched capacitor converter.
 */

#include "common.h"
#include "console.h"
#include "driver/ln9310.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

static int power_good;
static int startup_workaround_required;

int ln9310_power_good(void)
{
	return power_good;
}

static inline int raw_read8(int offset, int *value)
{
	return i2c_read8(ln9310_config.i2c_port, ln9310_config.i2c_addr_flags,
			 offset, value);
}

static inline int field_update8(int offset, int mask, int value)
{
	/* Clear mask and then set value in i2c reg value */
	return i2c_field_update8(ln9310_config.i2c_port,
				 ln9310_config.i2c_addr_flags, offset, mask,
				 value);
}

static void ln9310_irq_deferred(void)
{
	int status, val, pg_2to1, pg_3to1;

	status = raw_read8(LN9310_REG_INT1, &val);
	if (status) {
		CPRINTS("LN9310 reading INT1 failed");
		return;
	}

	CPRINTS("LN9310 received interrupt: 0x%x", val);
	/* Don't care other interrupts except mode change */
	if (!(val & LN9310_INT1_MODE))
		return;

	/* Check if the device is active in 2:1 or 3:1 switching mode */
	status = raw_read8(LN9310_REG_SYS_STS, &val);
	if (status) {
		CPRINTS("LN9310 reading SYS_STS failed");
		return;
	}
	CPRINTS("LN9310 system status: 0x%x", val);

	/* Either 2:1 or 3:1 mode active is treated as PGOOD */
	pg_2to1 = !!(val & LN9310_SYS_SWITCHING21_ACTIVE);
	pg_3to1 = !!(val & LN9310_SYS_SWITCHING31_ACTIVE);
	power_good = pg_2to1 || pg_3to1;
}
DECLARE_DEFERRED(ln9310_irq_deferred);

void ln9310_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ln9310_irq_deferred_data, 0);
}

static int is_battery_gt_10v(bool *out)
{
	int status, val;

	CPRINTS("LN9310 checking input voltage, threshold=10V");
	/*
	 * Turn on INFET_OUT_SWITCH_OK comparator;
	 * configure INFET_OUT_SWITCH_OK to 10V.
	 */
	status =
		field_update8(LN9310_REG_TRACK_CTRL,
			      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_MASK |
				      LN9310_TRACK_INFET_OUT_SWITCH_OK_CFG_MASK,
			      LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_ON |
				      LN9310_TRACK_INFET_OUT_SWITCH_OK_CFG_10V);
	if (status != EC_SUCCESS)
		return status;

	/* Read INFET_OUT_SWITCH_OK comparator */
	status = raw_read8(LN9310_REG_BC_STS_B, &val);
	if (status != EC_SUCCESS) {
		CPRINTS("LN9310 reading BC_STS_B failed");
		return status;
	}
	CPRINTS("LN9310 BC_STS_B: 0x%x", val);

	/*
	 * If INFET_OUT_SWITCH_OK=0, VIN < 10V
	 * If INFET_OUT_SWITCH_OK=1, VIN > 10V
	 */
	*out = !!(val & LN9310_BC_STS_B_INFET_OUT_SWITCH_OK);
	CPRINTS("LN9310 battery %s 10V", (*out) ? ">" : "<");

	/* Turn off INFET_OUT_SWITCH_OK comparator */
	status = field_update8(LN9310_REG_TRACK_CTRL,
			       LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_MASK,
			       LN9310_TRACK_INFET_OUT_SWITCH_OK_EN_OFF);

	return status;
}

static int ln9310_reset_detected(void)
{
	/*
	 * Check LN9310_REG_LION_CTRL to see if it has been reset to 0x0.
	 * ln9310_init() and all other functions will set this register
	 * to a non-zero value so it should only become 0 again if LN9310
	 * is reset.
	 */
	int val, status, reset_detected;

	status = raw_read8(LN9310_REG_LION_CTRL, &val);
	if (status) {
		CPRINTS("LN9310 reading LN9310_REG_LION_CTRL failed");
		/* If read fails, safest to assume reset has occurred */
		return 1;
	}
	reset_detected = (val == 0x0);
	if (reset_detected) {
		CPRINTS("LN9310 was reset (possibly in error)");
	}
	return reset_detected;
}

static int ln9310_update_startup_seq(void)
{
	int rc;

	CPRINTS("LN9310 update startup sequence");

	/*
	 * Startup sequence instruction swap to hold Cfly
	 * bottom plate low during startup
	 */
	rc = field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
			   LN9310_LION_CTRL_UNLOCK_AND_EN_TM);

	rc |= field_update8(LN9310_REG_SWAP_CTRL_0, 0xff, 0x52);

	rc |= field_update8(LN9310_REG_SWAP_CTRL_1, 0xff, 0x54);

	rc |= field_update8(LN9310_REG_SWAP_CTRL_2, 0xff, 0xCC);

	rc |= field_update8(LN9310_REG_SWAP_CTRL_3, 0xff, 0x02);

	/* Startup sequence settings */
	rc |= field_update8(
		LN9310_REG_CFG_4,
		LN9310_CFG_4_SC_OUT_PRECHARGE_EN_TIME_CFG_MASK |
			LN9310_CFG_4_SW1_VGS_SHORT_EN_MSK_MASK |
			LN9310_CFG_4_BSTH_BSTL_HIGH_ROUT_CFG_MASK,
		LN9310_CFG_4_SC_OUT_PRECHARGE_EN_TIME_CFG_ON |
			LN9310_CFG_4_SW1_VGS_SHORT_EN_MSK_OFF |
			LN9310_CFG_4_BSTH_BSTL_HIGH_ROUT_CFG_LOWEST);

	/* SW4 before BSTH_BSTL */
	rc |= field_update8(LN9310_REG_SPARE_0,
			    LN9310_SPARE_0_SW4_BEFORE_BSTH_BSTL_EN_CFG_MASK,
			    LN9310_SPARE_0_SW4_BEFORE_BSTH_BSTL_EN_CFG_ON);

	rc |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
			    LN9310_LION_CTRL_LOCK);

	return rc == EC_SUCCESS ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int ln9310_init_3to1(void)
{
	int rc;

	CPRINTS("LN9310 init (3:1 operation)");

	/* Enable track protection and SC_OUT configs for 3:1 switching */
	rc = field_update8(LN9310_REG_MODE_CHANGE_CFG,
			   LN9310_MODE_TM_TRACK_MASK |
				   LN9310_MODE_TM_SC_OUT_PRECHG_MASK |
				   LN9310_MODE_TM_VIN_OV_CFG_MASK,
			   LN9310_MODE_TM_TRACK_SWITCH31 |
				   LN9310_MODE_TM_SC_OUT_PRECHG_SWITCH31 |
				   LN9310_MODE_TM_VIN_OV_CFG_3S);

	/* Enable 3:1 operation mode */
	rc |= field_update8(LN9310_REG_PWR_CTRL, LN9310_PWR_OP_MODE_MASK,
			    LN9310_PWR_OP_MODE_SWITCH31);

	/* 3S lower bound delta configurations */
	rc |= field_update8(LN9310_REG_LB_CTRL, LN9310_LB_DELTA_MASK,
			    LN9310_LB_DELTA_3S);

	/*
	 * TODO(waihong): The LN9310_REG_SYS_CTR was set to a wrong value
	 * accidentally. Override it to 0. This may not need.
	 */
	rc |= field_update8(LN9310_REG_SYS_CTRL, 0xff, 0);

	return rc == EC_SUCCESS ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int ln9310_init_2to1(void)
{
	int rc;
	bool battery_gt_10v;

	CPRINTS("LN9310 init (2:1 operation)");

	rc = is_battery_gt_10v(&battery_gt_10v);
	if (rc != EC_SUCCESS || battery_gt_10v) {
		CPRINTS("LN9310 init stop. Input voltage is too high.");
		return EC_ERROR_UNKNOWN;
	}

	/* Enable track protection and SC_OUT configs for 2:1 switching */
	rc = field_update8(LN9310_REG_MODE_CHANGE_CFG,
			   LN9310_MODE_TM_TRACK_MASK |
				   LN9310_MODE_TM_SC_OUT_PRECHG_MASK,
			   LN9310_MODE_TM_TRACK_SWITCH21 |
				   LN9310_MODE_TM_SC_OUT_PRECHG_SWITCH21);

	/* Enable 2:1 operation mode */
	rc |= field_update8(LN9310_REG_PWR_CTRL, LN9310_PWR_OP_MODE_MASK,
			    LN9310_PWR_OP_MODE_SWITCH21);

	/* 2S lower bound delta configurations */
	rc |= field_update8(LN9310_REG_LB_CTRL, LN9310_LB_DELTA_MASK,
			    LN9310_LB_DELTA_2S);

	/*
	 * TODO(waihong): The LN9310_REG_SYS_CTR was set to a wrong value
	 * accidentally. Override it to 0. This may not need.
	 */
	rc |= field_update8(LN9310_REG_SYS_CTRL, 0xff, 0);

	return rc == EC_SUCCESS ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int ln9310_update_infet(void)
{
	int rc;

	CPRINTS("LN9310 update infet configuration");

	rc = field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
			   LN9310_LION_CTRL_UNLOCK_AND_EN_TM);

	/* Update Infet register settings */
	rc |= field_update8(LN9310_REG_CFG_5, LN9310_CFG_5_INGATE_PD_EN_MASK,
			    LN9310_CFG_5_INGATE_PD_EN_OFF);

	rc |= field_update8(LN9310_REG_CFG_5,
			    LN9310_CFG_5_INFET_CP_PD_BIAS_CFG_MASK,
			    LN9310_CFG_5_INFET_CP_PD_BIAS_CFG_LOWEST);

	/* enable automatic infet control */
	rc |= field_update8(LN9310_REG_PWR_CTRL,
			    LN9310_PWR_INFET_AUTO_MODE_MASK,
			    LN9310_PWR_INFET_AUTO_MODE_ON);

	/* disable LS_HELPER during IDLE by setting MSK bit high  */
	rc |= field_update8(LN9310_REG_CFG_0,
			    LN9310_CFG_0_LS_HELPER_IDLE_MSK_MASK,
			    LN9310_CFG_0_LS_HELPER_IDLE_MSK_ON);

	rc |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
			    LN9310_LION_CTRL_LOCK);

	return rc == EC_SUCCESS ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int ln9310_precharge_cfly(uint64_t *precharge_timeout)
{
	int status = 0;
	CPRINTS("LN9310 precharge cfly");

	/* Unlock registers and enable test mode */
	status |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
				LN9310_LION_CTRL_UNLOCK_AND_EN_TM);

	/* disable test mode overrides */
	status |= field_update8(LN9310_REG_FORCE_SC21_CTRL_2,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_MASK,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_OFF);

	/* Configure test mode target values for precharge ckts.  */
	status |= field_update8(
		LN9310_REG_FORCE_SC21_CTRL_1,
		LN9310_FORCE_SC21_CTRL_1_TM_SC_OUT_CFLY_PRECHARGE_MASK,
		LN9310_FORCE_SC21_CTRL_1_TM_SC_OUT_CFLY_PRECHARGE_ON);

	/* Force SCOUT precharge/predischarge overrides */
	status |= field_update8(
		LN9310_REG_TEST_MODE_CTRL,
		LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PRECHARGE_MASK |
			LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PREDISCHARGE_MASK,
		LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PRECHARGE_ON |
			LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PREDISCHARGE_ON);

	/* Force enable CFLY precharge overrides */
	status |= field_update8(LN9310_REG_FORCE_SC21_CTRL_2,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_MASK,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_ON);

	/* delay long enough to ensure CFLY has time to fully precharge */
	crec_usleep(LN9310_CFLY_PRECHARGE_DELAY);

	/* locking and leaving test mode will stop CFLY precharge */
	*precharge_timeout = get_time().val + LN9310_CFLY_PRECHARGE_TIMEOUT;
	status |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
				LN9310_LION_CTRL_LOCK);

	return status;
}

static int ln9310_precharge_cfly_reset(void)
{
	int status = 0;
	CPRINTS("LN9310 precharge cfly reset");

	/* set known initial state for config bits related to cfly precharge */
	status |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
				LN9310_LION_CTRL_UNLOCK);

	/* Force off SCOUT precharge/predischarge overrides */
	status |= field_update8(
		LN9310_REG_TEST_MODE_CTRL,
		LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PRECHARGE_MASK |
			LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PREDISCHARGE_MASK,
		LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PRECHARGE_OFF |
			LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PREDISCHARGE_OFF);

	/* disable test mode overrides */
	status |= field_update8(LN9310_REG_FORCE_SC21_CTRL_2,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_MASK,
				LN9310_FORCE_SC21_CTRL_2_FORCE_SW_CTRL_REQ_OFF);

	/* disable CFLY and SC_OUT precharge control  */
	status |= field_update8(
		LN9310_REG_FORCE_SC21_CTRL_1,
		LN9310_FORCE_SC21_CTRL_1_TM_SC_OUT_CFLY_PRECHARGE_MASK,
		LN9310_FORCE_SC21_CTRL_1_TM_SC_OUT_CFLY_PRECHARGE_OFF);

	status |= field_update8(LN9310_REG_LION_CTRL, LN9310_LION_CTRL_MASK,
				LN9310_LION_CTRL_LOCK);

	return status;
}

int ln9310_init(void)
{
	int status, val, chip_revision;
	enum battery_cell_type batt;

	/* Make sure initial state of LN9310 is STANDBY (i.e. output is off)  */
	field_update8(LN9310_REG_STARTUP_CTRL, LN9310_STARTUP_STANDBY_EN, 1);

	/*
	 * LN9310 software startup is only required for earlier silicon revs.
	 * LN9310 hardware revisions after LN9310_BC_STS_C_CHIP_REV_FIXED
	 * should not use the software startup sequence.
	 */
	status = raw_read8(LN9310_REG_BC_STS_C, &val);
	if (status != EC_SUCCESS) {
		CPRINTS("LN9310 reading BC_STS_C failed: %d", status);
		return status;
	}
	chip_revision = val & LN9310_BC_STS_C_CHIP_REV_MASK;
	startup_workaround_required = chip_revision <
				      LN9310_BC_STS_C_CHIP_REV_FIXED;

	/* Update INFET configuration  */
	status = ln9310_update_infet();

	if (status != EC_SUCCESS)
		return status;

	/*
	 * Set OPERATION_MODE update method
	 *   - OP_MODE_MANUAL_UPDATE = 0
	 *   - OP_MODE_SELF_SYNC_EN  = 1
	 */
	field_update8(LN9310_REG_PWR_CTRL,
		      LN9310_PWR_OP_MODE_MANUAL_UPDATE_MASK,
		      LN9310_PWR_OP_MODE_MANUAL_UPDATE_OFF);

	field_update8(LN9310_REG_TIMER_CTRL, LN9310_TIMER_OP_SELF_SYNC_EN_MASK,
		      LN9310_TIMER_OP_SELF_SYNC_EN_ON);

	/*
	 * Use VIN for VDR, not EXT_5V. The following usleep will give
	 * circuit time to settle.
	 */
	field_update8(LN9310_REG_STARTUP_CTRL,
		      LN9310_STARTUP_SELECT_EXT_5V_FOR_VDR, 0);

	field_update8(LN9310_REG_LB_CTRL, LN9310_LB_MIN_FREQ_EN,
		      LN9310_LB_MIN_FREQ_EN);

	/* Set minimum switching frequency to 25 kHz */
	field_update8(LN9310_REG_SPARE_0, LN9310_SPARE_0_LB_MIN_FREQ_SEL_MASK,
		      LN9310_SPARE_0_LB_MIN_FREQ_SEL_ON);

	crec_usleep(LN9310_CDC_DELAY);
	CPRINTS("LN9310 OP_MODE Update method: Self-sync");

	if (startup_workaround_required) {
		/* Update Startup sequence */
		status = ln9310_update_startup_seq();
		if (status != EC_SUCCESS)
			return status;
	}

	batt = board_get_battery_cell_type();
	if (batt == BATTERY_CELL_TYPE_3S) {
		status = ln9310_init_3to1();
	} else if (batt == BATTERY_CELL_TYPE_2S) {
		status = ln9310_init_2to1();
	} else {
		CPRINTS("LN9310 not supported battery type: %d", batt);
		return EC_ERROR_INVAL;
	}

	if (status != EC_SUCCESS)
		return status;

	/* Unmask the MODE change interrupt */
	field_update8(LN9310_REG_INT1_MSK, LN9310_INT1_MODE, 0);
	return EC_SUCCESS;
}

void ln9310_software_enable(int enable)
{
	int status, val, retry_count;
	uint64_t precharge_timeout;
	bool ln9310_init_completed = false;

	/*
	 * LN9310 startup requires (nEN=0 AND STANDBY_EN=0) where nEN is a pin
	 * and STANDBY_EN is a register bit. Previous EC firmware set
	 * STANDBY_EN=1 in ln9310_init and toggled nEN to startup/shutdown. In
	 * addition to normal startup, this function also implements an
	 * alternate software (i.e. controlled by EC through I2C commands)
	 * startup sequence required by older chip versions, so one option is to
	 * set nEN=1 and just used ln9310_software_enable to startup/shutdown.
	 * ln9310_software_enable can also be used in conjunction w/ the nEN pin
	 * (in case nEN pin is desired as visible signal ) as follows:
	 *
	 * Initial battery insertion:
	 * nEN=1
	 * ln9310_init() - initial condition is STANDBY_EN=1
	 *
	 * Power up LN9310:
	 * nEN=0 - STANDBY_EN should be 1 so this doesn't trigger startup
	 * ln9310_software_enable(1) - triggers alternate software-based startup
	 *
	 * Power down LN9310:
	 * nEN=1 - shutdown LN9310 (shutdown seq. does not require modification)
	 * ln9310_software_enable(0) - reset LN9310 register to state necessary
	 *                             for subsequent startups
	 */
	if (ln9310_reset_detected())
		ln9310_init();

	/* Dummy clear all interrupts */
	status = raw_read8(LN9310_REG_INT1, &val);
	if (status) {
		CPRINTS("LN9310 reading INT1 failed");
		return;
	}
	CPRINTS("LN9310 cleared interrupts: 0x%x", val);

	if (startup_workaround_required) {
		if (enable) {
			/*
			 * Software modification of LN9310 startup sequence w/
			 * retry loop.
			 *
			 * (1) Clear interrupts
			 * (2) Precharge Cfly w/ overrides of internal LN9310
			 * signals (3) disable overrides -> stop precharging
			 * Cfly (4.1) if < 100 ms elapsed since (2) -> trigger
			 * LN9310 internal startup seq. (4.2) else -> abort and
			 * optionally retry from step 2
			 */
			retry_count = 0;
			while (!ln9310_init_completed &&
			       retry_count < LN9310_INIT_RETRY_COUNT) {
				/* Precharge CFLY before starting up */
				status = ln9310_precharge_cfly(
					&precharge_timeout);
				if (status != EC_SUCCESS) {
					CPRINTS("LN9310 failed to run Cfly precharge sequence");
					status = ln9310_precharge_cfly_reset();
					retry_count++;
					continue;
				}

				/*
				 * Only start the SC if the cfly precharge
				 * hasn't timed out (i.e. ended too long ago)
				 */
				if (get_time().val < precharge_timeout) {
					/* Clear the STANDBY_EN bit to enable
					 * the SC */
					field_update8(LN9310_REG_STARTUP_CTRL,
						      LN9310_STARTUP_STANDBY_EN,
						      0);
					if (get_time().val >
					    precharge_timeout) {
						/*
						 * if timed out during previous
						 * I2C command, abort startup
						 * attempt
						 */
						field_update8(
							LN9310_REG_STARTUP_CTRL,
							LN9310_STARTUP_STANDBY_EN,
							1);
					} else {
						/* all other paths should
						 * reattempt startup  */
						ln9310_init_completed = true;
					}
				}
				/* Reset to known state for config bits related
				 * to cfly precharge */
				ln9310_precharge_cfly_reset();
				retry_count++;
			}

			if (!ln9310_init_completed) {
				CPRINTS("LN9310 failed to start after %d retry attempts",
					retry_count);
			}
		} else {
			/*
			 * Internal LN9310 shutdown sequence is ok as is, so
			 * just reset the state to prepare for subsequent
			 * startup sequences.
			 *
			 * (1) set STANDBY_EN=1 to be sure the part turns off
			 * even if nEN=0 (2) reset cfly precharge related
			 * registers to known initial state
			 */
			field_update8(LN9310_REG_STARTUP_CTRL,
				      LN9310_STARTUP_STANDBY_EN, 1);

			ln9310_precharge_cfly_reset();
		}
	} else {
		/*
		 * for newer LN9310 revsisions, the startup workaround is not
		 * required so the STANDBY_EN bit can just be set directly
		 */
		field_update8(LN9310_REG_STARTUP_CTRL,
			      LN9310_STARTUP_STANDBY_EN, !enable);
	}
	return;
}

__test_only void ln9310_reset_to_initial_state(void)
{
	power_good = 0;
	startup_workaround_required = 0;
}
