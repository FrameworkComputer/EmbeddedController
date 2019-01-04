/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Board specific routines used only when BOARD_CLOSED_SOURCE_SET1 is
 * enabled.
 */
#include "ccd_config.h"
#include "closed_source_set1.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"

#define CPRINTF(format, args...) \
	cprintf(CC_SYSTEM, "Closed Source Set1: " format, ## args)

/*
 * Map common gpio.inc pin names to descriptive names specific to the
 * BOARD_CLOSED_SOURCE_SET1 option.
 */
#define GPIO_FACTORY_MODE		GPIO_I2C_SCL_INA
#define GPIO_CHROME_SEL			GPIO_EN_PP3300_INA_L
#define GPIO_EXIT_FACTORY_MODE		GPIO_I2C_SDA_INA

enum ec_trust_level {
	EC_TL_FACTORY_MODE,
	EC_TL_DIAGNOSTIC_MODE,
	EC_TL_COREBOOT,
};

void closed_source_set1_configure_gpios(void)
{
	CPRINTF("configuring GPIOs\n");

	/*
	 * Connect GPIO outputs to pads:
	 *     GPIO0_12 (FACTORY_MODE)      : B0
	 *     GPIO0_13 (EXIT_FACTORY_MODE) : B1
	 *     GPIO0_11 (CHROME_SEL)        : B7
	 */
	GWRITE(PINMUX, DIOB0_SEL, GC_PINMUX_GPIO0_GPIO12_SEL);
	GWRITE(PINMUX, DIOB1_SEL, GC_PINMUX_GPIO0_GPIO13_SEL);
	GWRITE(PINMUX, DIOB7_SEL, GC_PINMUX_GPIO0_GPIO11_SEL);

	/*
	 * The PINMUX entries in gpio.inc already write to the GPIOn_GPOIn_SEL
	 * and DIOBn_CTL registers with values that work for GPIO output
	 * operation. If gpio.inc makes changes to the GPIO_I2C_SCL_INA,
	 * GPIO_I2C_SDA_INA, or GPIO_EN_PP3300_INA_L pinmux, then explicitly
	 * configure the corresponding GPIOn_GPIOn_SEL and DIOBn_CTL registers
	 * here.
	 */

	/*
	 * TODO (keithshort): closed source EC documentation defines
	 * EXIT_FACTORY_MODE as an output from the EC that is driven low
	 * to indicate that factory mode must be terminated.  However, the
	 * EC firmware has not yet (and may never) add this capability.
	 */

	closed_source_set1_update_factory_mode();
}

static void closed_source_set1_update_ec_trust_level(enum ec_trust_level tl)
{
	/*
	 * The EC state is partially controlled by the FACTORY_MODE and
	 * CHROME_SEL signals.
	 *
	 * State                          Description
	 * CHROME_SEL=0,FACTORY_MODE=1    TL0: EC factory mode
	 * CHROME_SEL=0,FACTORY_MODE=0    TL1: EC diagnostic mode
	 * CHROME_SEL=1,FACTORY_MODE=0    TL2: EC coreboot mode
	 * CHROME_SEL=1,FACTORY_MODE=1    Undefined
	 */
	switch (tl) {
	case EC_TL_FACTORY_MODE:
		CPRINTF("enable factory mode\n");
		/*
		 * Enable factory mode, CHROME_SEL must be set low first so
		 * that CHROME_SEL and FACTORY_MODE are not high
		 * simultaneously.
		 */
		gpio_set_flags(GPIO_CHROME_SEL, GPIO_OUT_LOW);
		gpio_set_flags(GPIO_FACTORY_MODE, GPIO_OUT_HIGH);
		break;

	case EC_TL_DIAGNOSTIC_MODE:
		CPRINTF("enable diagnostic mode\n");

		gpio_set_flags(GPIO_CHROME_SEL, GPIO_OUT_LOW);
		gpio_set_flags(GPIO_FACTORY_MODE, GPIO_OUT_LOW);
		break;

	case EC_TL_COREBOOT:
		CPRINTF("disable factory mode\n");
		/*
		 * Disable factory mode, set FACTORY_MODE low first to avoid
		 * undefined state.
		 */
		gpio_set_flags(GPIO_FACTORY_MODE, GPIO_OUT_LOW);
		gpio_set_flags(GPIO_CHROME_SEL, GPIO_OUT_HIGH);
		break;
	default:
		CPRINTF("unsupported EC trust level %d\n", tl);
	}
}

void closed_source_set1_update_factory_mode(void)
{
	if (ccd_get_factory_mode())
		closed_source_set1_update_ec_trust_level(EC_TL_FACTORY_MODE);
	else
		closed_source_set1_update_ec_trust_level(EC_TL_COREBOOT);
}

void close_source_set1_disable_tpm(void)
{
	/*
	 * Once the TPM mode is disabled from the AP, set the EC trust level
	 * to permit running diagnostics.  Diagnostic mode may be entered from
	 * any of the EC trust level states, so no additional checks are needed.
	 *
	 * This state is only cleared by a reboot of the Cr50 and then the
	 * trust level reverts back to either EC_TL_FACTORY_MODE or
	 * EC_TL_COREBOOT.
	 */
	closed_source_set1_update_ec_trust_level(EC_TL_DIAGNOSTIC_MODE);
}


#ifdef CR50_DEV
/* Debug command to manually set the EC trust level */
static int ec_trust_level(int argc, char **argv)
{
	enum ec_trust_level tl;

	if (argc > 1) {
		tl = (enum ec_trust_level)atoi(argv[1]);

		closed_source_set1_update_ec_trust_level(tl);
	}

	ccprintf("CCD factory mode  = %d\n", ccd_get_factory_mode());

	ccprintf("FACTORY_MODE      = %d\n",
		gpio_get_level(GPIO_FACTORY_MODE));
	ccprintf("CHROME_SEL        = %d\n",
		gpio_get_level(GPIO_CHROME_SEL));
	ccprintf("EXIT_FACTORY_MODE = %d\n",
		gpio_get_level(GPIO_EXIT_FACTORY_MODE));

	return 0;
}
DECLARE_SAFE_CONSOLE_COMMAND(ectrust, ec_trust_level,
	"[0|1|2]",
	"Get/set the EC trust level");
#endif

