/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "battery.h"
#include "board.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "i2c.h"
#include "lb_common.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)



void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	/* TODO */
}

static void chipset_force_g3(void)
{
	gpio_set_level(GPIO_SUSP_L, 0);
	gpio_set_level(GPIO_EC_VCCST_PG, 0);
	gpio_set_level(GPIO_VR_ON, 0);
	gpio_set_level(GPIO_PCH_PWROK, 0);
	gpio_set_level(GPIO_SYS_PWROK, 0);
	gpio_set_level(GPIO_SYSON, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
	gpio_set_level(GPIO_EC_KBL_PWR_EN, 0);
	gpio_set_level(GPIO_PCH_PWR_EN, 0);
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_EC_ON, 0);
}

void chipset_reset(enum chipset_reset_reason reason)
{
	/* TODO */
}

void chipset_throttle_cpu(int throttle)
{
	/* TODO */
}

int board_chipset_power_on(void)
{
	gpio_set_level(GPIO_EC_ON, 1);

	msleep(5);

	if (power_wait_signals(IN_PGOOD_PWR_3V5V)) {
		CPRINTS("timeout waiting for PWR_3V5V_PG");
		chipset_force_g3();
		return false;
	}

	msleep(30);

	gpio_set_level(GPIO_PCH_DPWROK, 1);

	if (power_wait_signals(IN_PCH_SLP_SUS_DEASSERTED)) {
		CPRINTS("timeout waiting for SLP_SUS deassert");
		chipset_force_g3();
		return false;
	}

	msleep(10);

	gpio_set_level(GPIO_PCH_PWR_EN, 1);

	msleep(5);

	if (power_wait_signals(IN_PGOOD_VCCIN_AUX_VR)) {
		CPRINTS("timeout waiting for VCCIN_AUX_VR_PG");
		chipset_force_g3();
		return false;
	}

	/* Add 10ms delay between SUSP_VR and RSMRST */
	msleep(20);

	/* Deassert RSMRST# */
	gpio_set_level(GPIO_PCH_RSMRST_L, 1);
	return true;
}

enum power_state power_chipset_init(void)
{
	chipset_force_g3();
	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	//struct batt_params batt;

	switch (state) {
	case POWER_G3:
		CPRINTS("power handle state in G3");
		if (extpower_is_present()) {
			/* AC-mode enable power on signal */
			if (board_chipset_power_on()){
				CPRINTS("Chipset power on in AC mode");
				gpio_set_level(GPIO_AC_PRESENT_OUT, 1);
			}
		} else {
			/* DC-mode disable power on signal */
			CPRINTS("Chipset power off to G3 in DC mode");
			chipset_force_g3();
		}
		break;

	case POWER_S5:
		CPRINTS("power handle state in S5");

		if (power_wait_signals(IN_PCH_SLP_S4_DEASSERTED)) {
			CPRINTS("timeout waiting for S4 exit");
			return POWER_S5G3;
		}

		return POWER_S5S3; /* Power up to next state */

		break;

	case POWER_S3:
		CPRINTS("power handle state in S3");

        if (power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}

		break;

	case POWER_S0:
		CPRINTS("power handle state in S0");
		if ((power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}
		break;

	case POWER_G3S5:
		CPRINTS("power handle state in G3S5");

		if (board_chipset_power_on()) {
			return POWER_S5;
		} else {
			return POWER_G3;
		}
		break;

	case POWER_S5S3:
		CPRINTS("power handle state in S5S3");

        gpio_set_level(GPIO_SYSON, 1);

        /* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

		break;

	case POWER_S3S0:
		CPRINTS("power handle state in S3S0");

        gpio_set_level(GPIO_SUSP_L, 1);

        msleep(10);

        gpio_set_level(GPIO_EC_VCCST_PG, 1);

        msleep(30);

        gpio_set_level(GPIO_VR_ON, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

        if (power_wait_signals(IN_PGOOD_PWR_VR)) {
			gpio_set_level(GPIO_SUSP_L, 0);
            gpio_set_level(GPIO_EC_VCCST_PG, 0);
            gpio_set_level(GPIO_VR_ON, 0);
			return POWER_S3;
		}

        gpio_set_level(GPIO_PCH_PWROK, 1);

        msleep(10);

        gpio_set_level(GPIO_SYS_PWROK, 1);

        return POWER_S0;

		break;

	case POWER_S0S3:
		CPRINTS("power handle state in S0S3");
		gpio_set_level(GPIO_SUSP_L, 0);
        gpio_set_level(GPIO_EC_VCCST_PG, 0);
        gpio_set_level(GPIO_VR_ON, 0);
		gpio_set_level(GPIO_PCH_PWROK, 0);
		gpio_set_level(GPIO_SYS_PWROK, 0);
		return POWER_S3;
		break;

	case POWER_S3S5:
		CPRINTS("power handle state in S3S5");
		gpio_set_level(GPIO_SYSON, 0);
		return POWER_S5;
		break;

	case POWER_S5G3:
		CPRINTS("power handle state in S5G3");
		return POWER_G3;
		break;
	}

	return state;
}
