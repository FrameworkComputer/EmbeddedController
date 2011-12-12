/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* x86 power control module for Chrome EC */

#include "registers.h"
#include "x86_power.h"

/* Signals to/from EC.  These are ALL the signals on the schematic
 * that seem related.  From the 11/14 schematic. */
#if 0
enum ec_signal {
};

/* Signals from Chief River Platform Power Sequence document.  Directions are
 * from the EC's point of view. */
enum x86_signal {
	// Schematic        GPIO  dir  Intel doc name
	CPU1.5V_S3_GATE,     PH5, out  /* Connected to SUSP# via resistor.  Can
					* disable VDDQ when driven low. */
	PBTN_OUT#,           PG3, out  PWRBTN#
	PCH_DPWROK,          PG0, out, DPWROK
	PCH_PWROK,           PC5, out, PWROK      /* AND'd with VGATE (CPU_CORE
						   * good), then connected to
						   * PWROK.  Also * connected to
						   * APWROK via resistor */
	PCH_RSMRST#,         PF1, out, RSMRST#    /* Also connected to
						   * PCH_DPWROK via resistor */
	PM_SLP_SUS#,         PD3, inp, SLP_SUS
	SLP_A#,              PG5, i/o, SLP_A#     /* Intel claims inp; why does
						   * schematic claim I/O? */
	SLP_ME_CSW_DEV#,     PG4, i/o, SLP_ME_CSW_DEV#  /* Intel also claims
							 * inp? */
	PM_SLP_S3#,          PJ0, inp, SLP_S3#
	PM_SLP_S4#,          PJ1, inp, SLP_S4#
	PM_SLP_S5#,          PJ2, inp, SLP_S5#
	SUSACK#,             PD2, out, SUS_ACK#   /* Also connected to SUSWARN#
						   * via (no-load) resistor */
	SUSWARN#,            PH1, inp, SUSWARN#

	/* On EC schematic, but not mentioned directly in doc */
	ACOFF,               PG2, inp  /* Not connected? */
	BKOFF#,              PH4, out  /* Turns off display when pulled low.
					* Can tri-state when not using. */
	EC_ACIN,             PC7, inp  /* Connected to ACIN, which comes from
					* ACOK on charger */
	EC_KBRST#,           PQ7, out, RCIN#
	EC_LID_OUT#,         PF0, out, GPIO15
	EC_ON,               PC6, out
	ENBKL,               PF4, inp, L_BKLTEN  /* From Panther Point LVDS */
	GATEA20,             PQ6, out, A20GATE
	HDA_SDO,             PG1, out, HDA_SDO
	H_PROCHOT#_EC,       PF2, out, PROCHOT#  /* Ivy Bridge.  Has pullup. */
	SA_PGOOD,            PF3, inp /* Set by power control when VccSA is
				       * good */
	SMI#,                PJ3, out, GPIO8
	SUSP#,               PG6, out /* Disables VCCP, VDDQ, +5VS, +3VS,
				       * +1.5VS, +0.75VS, +1.8VS */
	SYSON,               PB6, out /* Enables +1.5VP, VCCP */
	VR_ON,               PB7, out  /* Enables CPU_CORE, VGFX_CORE */
};


/* Signals from the Ivy Bridge power sequencing document that aren't connected
 * to the EC (at least, directly) */
enum x86_signal_no_control {
	// Platform to PCH
	ACPRESENT,   /* Comes from ACIN, which comes from ACOK on charger.
		      * There's an EC_ACIN line mentioned, but it doesn't seem
		      * to go to the EC(!) */
	IMVP7_VR_EN, /* (not on schematic) */
	RTCRST#,     /* Can short to ground via jumper */
	SA_VR_PWROK, /* (not on schematic) */
	SYS_PWROK,
	VR_VDDPWRGD,

	// PCH to platform
	CL_RST#
	SUSCLK,
	SUS_STAT#,
	SLP_LAN#

	// Platform to platform
	ALL_SYS_PWRGD,

	// Power rails
	VccASW,
	VccAXG,
	VccCore (CPU),  // aka Vboot
	VccCore (PCH),
	VccDSW,
	VccRTC,
	VCCP,           // aka VccIO
	VccSA,
	VccSPI,
	VccSUS,
	Vcc_WLAN,
	VDDQ,
	// (+ all platform rails?)
};

/* Signals we explicitly don't care about */
enum x86_signal_dont_care {
	SUSPWRDNACK,  // Only applicable if deep sleep well not supported

};


#endif




/* Signal definitions are messy, split across multiple GPIOs.
 * Fortunately, we only need to set them one at a time. */
struct signal_gpio {
	int is_output;

};

struct signal_gpio gpios[] =  {



};




int x86_power_init(void)
{
	return EC_SUCCESS;
}

