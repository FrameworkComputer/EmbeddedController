/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek rt9466/rt9467 battery charger driver.
 */

#ifndef __CROS_EC_RT946X_H
#define __CROS_EC_RT946X_H

/* Charger parameters */
#define CHARGER_NAME	RT946X_CHARGER_NAME
#define CHARGE_V_MAX	4710
#define CHARGE_V_MIN	3900
#define CHARGE_V_STEP	10
#define CHARGE_I_MAX	5000
#define CHARGE_I_MIN	100
#define CHARGE_I_OFF	0
#define CHARGE_I_STEP	100
#define INPUT_I_MAX	3250
#define INPUT_I_MIN	100
#define INPUT_I_STEP	50

/* Registers */
#define RT946X_REG_CORECTRL0		0x00
#define RT946X_REG_CHGCTRL1		0x01
#define RT946X_REG_CHGCTRL2		0x02
#define RT946X_REG_CHGCTRL3		0x03
#define RT946X_REG_CHGCTRL4		0x04
#define RT946X_REG_CHGCTRL5		0x05
#define RT946X_REG_CHGCTRL6		0x06
#define RT946X_REG_CHGCTRL7		0x07
#define RT946X_REG_CHGCTRL8		0x08
#define RT946X_REG_CHGCTRL9		0x09
#define RT946X_REG_CHGCTRL10		0x0A
#define RT946X_REG_CHGCTRL11		0x0B
#define RT946X_REG_CHGCTRL12		0x0C
#define RT946X_REG_CHGCTRL13		0x0D
#define RT946X_REG_CHGCTRL14		0x0E
#define RT946X_REG_CHGCTRL15		0x0F
#define RT946X_REG_CHGCTRL16		0x10
#define RT946X_REG_CHGADC		0x11

#ifdef CONFIG_CHARGER_RT9467
#define RT946X_REG_DPDM1		0x12
#define RT946X_REG_DPDM2		0x13
#define RT946X_REG_DPDM3		0x14
#endif

#define RT946X_REG_CHGCTRL19		0x18
#define RT946X_REG_CHGCTRL17		0x19
#define RT946X_REG_CHGCTRL18		0x1A
#define RT946X_REG_CHGHIDDENCTRL2	0x21
#define RT946X_REG_CHGHIDDENCTRL4	0x23
#define RT946X_REG_CHGHIDDENCTRL6	0x25
#define RT946X_REG_CHGHIDDENCTRL7	0x26
#define RT946X_REG_CHGHIDDENCTRL8	0x27
#define RT946X_REG_CHGHIDDENCTRL9	0x28
#define RT946X_REG_CHGHIDDENCTRL15	0x2E
#define RT946X_REG_DEVICEID		0x40
#define RT946X_REG_CHGSTAT		0x42
#define RT946X_REG_CHGNTC		0x43
#define RT946X_REG_ADCDATAH		0x44
#define RT946X_REG_ADCDATAL		0x45
#define RT946X_REG_CHGSTATC		0x50
#define RT946X_REG_CHGFAULT		0x51
#define RT946X_REG_TSSTATC		0x52
#define RT946X_REG_CHGIRQ1		0x53
#define RT946X_REG_CHGIRQ2		0x54
#define RT946X_REG_CHGIRQ3		0x55

#ifdef CONFIG_CHARGER_RT9467
#define RT946X_REG_DPDMIRQ		0x56
#endif

#define RT946X_REG_CHGSTATCCTRL		0x60
#define RT946X_REG_CHGFAULTCTRL		0x61
#define RT946X_REG_TSSTATCCTRL		0x62
#define RT946X_REG_CHGIRQ1CTRL		0x63
#define RT946X_REG_CHGIRQ2CTRL		0x64
#define RT946X_REG_CHGIRQ3CTRL		0x65

#ifdef CONFIG_CHARGER_RT9467
#define RT946X_REG_DPDMIRQCTRL		0x66
#endif

/* EOC current */
#define RT946X_IEOC_MIN		100
#define RT946X_IEOC_MAX		850
#define RT946X_IEOC_STEP	50

/* Minimum Input Voltage Regulator */
#define RT946X_MIVR_MIN		3900
#define RT946X_MIVR_MAX		13400
#define RT946X_MIVR_STEP	100

/* Boost voltage */
#define RT946X_BOOST_VOLTAGE_MIN	4425
#define RT946X_BOOST_VOLTAGE_MAX	5825
#define RT946X_BOOST_VOLTAGE_STEP	25

/* IR compensation resistor */
#define RT946X_IRCMP_RES_MIN	0
#define RT946X_IRCMP_RES_MAX	175
#define RT946X_IRCMP_RES_STEP	25

/* IR compensation voltage clamp */
#define RT946X_IRCMP_VCLAMP_MIN		0
#define RT946X_IRCMP_VCLAMP_MAX		224
#define RT946X_IRCMP_VCLAMP_STEP	32

/* Pre-charge mode threshold voltage */
#define RT946X_VPREC_MIN		2000
#define RT946X_VPREC_MAX		3500
#define RT946X_VPREC_STEP		100

/* Pre-charge current */
#define RT946X_IPREC_MIN		100
#define RT946X_IPREC_MAX		850
#define RT946X_IPREC_STEP		50

/* AICLVTH */
#define RT946X_AICLVTH_MIN	4100
#define RT946X_AICLVTH_MAX	4800
#define RT946X_AICLVTH_STEP	100

/* NTC */
#define RT946X_BATTEMP_NORMAL	0x00
#define RT946X_BATTEMP_WARM	0x02
#define RT946X_BATTEMP_COOL	0x03
#define RT946X_BATTEMP_COLD	0x05
#define RT946X_BATTEMP_HOT	0x06

/* ========== CORECTRL0 0x00 ============ */
#define RT946X_SHIFT_RST	7

#define RT946X_MASK_RST		(1 << RT946X_SHIFT_RST)

/* ========== CHGCTRL1 0x01 ============ */
#define RT946X_SHIFT_OPA_MODE   0
#define RT946X_SHIFT_HZ_EN      2

#define RT946X_MASK_OPA_MODE	(1 << RT946X_SHIFT_OPA_MODE)
#define RT946X_MASK_HZ_EN	(1 << RT946X_SHIFT_HZ_EN)

/* ========== CHGCTRL2 0x02 ============ */
#define RT946X_SHIFT_SHIP_MODE	7
#define RT946X_SHIFT_TE		4
#define RT946X_SHIFT_ILMTSEL	2
#define RT946X_SHIFT_CHG_EN	0

#define RT946X_MASK_SHIP_MODE	(1 << RT946X_SHIFT_SHIP_MODE)
#define RT946X_MASK_TE		(1 << RT946X_SHIFT_TE)
#define RT946X_MASK_ILMTSEL	(0x3 << RT946X_SHIFT_ILMTSEL)
#define RT946X_MASK_CHG_EN	(1 << RT946X_SHIFT_CHG_EN)

/* ========== CHGCTRL3 0x03 ============ */
#define RT946X_SHIFT_AICR	2
#define RT946X_SHIFT_ILIMEN	0

#define RT946X_MASK_AICR	(0x3F << RT946X_SHIFT_AICR)
#define RT946X_MASK_ILIMEN	(1 << RT946X_SHIFT_ILIMEN)

/* ========== CHGCTRL4 0x04 ============ */
#define RT946X_SHIFT_CV	1

#define RT946X_MASK_CV	0xFE

/* ========== CHGCTRL5 0x05 ============ */
#define RT946X_SHIFT_BOOST_VOLTAGE	2

#define RT946X_MASK_BOOST_VOLTAGE	0xFC

/* ========== CHGCTRL6 0x06 ============ */
#define RT946X_SHIFT_MIVR	1

#define RT946X_MASK_MIVR	(0x7F << RT946X_SHIFT_MIVR)

/* ========== CHGCTRL7 0x07 ============ */
#define RT946X_SHIFT_ICHG	2

#define RT946X_MASK_ICHG	(0x3F << RT946X_SHIFT_ICHG)

/* ========== CHGCTRL8 0x08 ============ */
#define RT946X_SHIFT_VPREC	4
#define RT946X_SHIFT_IPREC	0

#define RT946X_MASK_VPREC	(0xF << RT946X_SHIFT_VPREC)
#define RT946X_MASK_IPREC	(0xF << RT946X_SHIFT_IPREC)

/* ========== CHGCTRL9 0x09 ============ */
#define RT946X_SHIFT_IEOC	4

#define RT946X_MASK_IEOC	(0xF << RT946X_SHIFT_IEOC)

/* ========== CHGCTRL10 0x0A ============ */
#define RT946X_SHIFT_BOOST_CURRENT	0

#define RT946X_MASK_BOOST_CURRENT	0x07

/* ========== CHGCTRL12 0x0C ============ */
#define RT946X_SHIFT_TMR_EN	1

#define RT946X_MASK_TMR_EN	(1 << RT946X_SHIFT_TMR_EN)

/* ========== CHGCTRL13 0x0D ============ */
#define RT946X_SHIFT_WDT_EN	7

#define RT946X_MASK_WDT_EN	(1 << RT946X_SHIFT_WDT_EN)

/* ========== CHGCTRL14 0x0E ============ */
#define RT946X_SHIFT_AICLMEAS	7
#define RT946X_SHIFT_AICLVTH	0

#define RT946X_MASK_AICLMEAS	(1 << RT946X_SHIFT_AICLMEAS)
#define RT946X_MASK_AICLVTH	0x07

/* ========== CHGCTRL16 0x10 ============ */
#define RT946X_SHIFT_JEITA_EN	4

#define RT946X_MASK_JEITA_EN	(1 << RT946X_SHIFT_JEITA_EN)

/* ========== CHGADC 0x11 ============ */
#define RT946X_SHIFT_ADC_IN_SEL	4
#define RT946X_SHIFT_ADC_START	0

#define RT946X_MASK_ADC_IN_SEL	(0xF << RT946X_SHIFT_ADC_IN_SEL)
#define RT946X_MASK_ADC_START	(1 << RT946X_SHIFT_ADC_START)

/* ========== CHGDPDM1 0x12 ============ */
#define RT946X_SHIFT_USBCHGEN	7
#define RT946X_SHIFT_DCP	2
#define RT946X_SHIFT_CDP	1
#define RT946X_SHIFT_SDP	0

#define RT946X_MASK_USBCHGEN	(1 << RT946X_SHIFT_USBCHGEN)
#define RT946X_MASK_DCP		(1 << RT946X_SHIFT_DCP)
#define RT946X_MASK_CDP		(1 << RT946X_SHIFT_CDP)
#define RT946X_MASK_SDP		(1 << RT946X_SHIFT_SDP)

#define RT946X_MASK_BC12_TYPE	(RT946X_MASK_DCP | \
				 RT946X_MASK_CDP | \
				 RT946X_MASK_SDP)

/* ========== CHGCTRL18 0x1A ============ */
#define RT946X_SHIFT_IRCMP_RES		3
#define RT946X_SHIFT_IRCMP_VCLAMP	0

#define RT946X_MASK_IRCMP_RES		(0x7 << RT946X_SHIFT_IRCMP_RES)
#define RT946X_MASK_IRCMP_VCLAMP	(0x7 << RT946X_SHIFT_IRCMP_VCLAMP)

/* ========== DEVICE_ID 0x40 ============ */
#define RT946X_MASK_VENDOR_ID	0xF0
#define RT946X_MASK_CHIP_REV	0x0F

/* ========== CHGSTAT 0x42 ============ */
#define RT946X_SHIFT_CHG_STAT	6
#define RT946X_SHIFT_ADC_STAT	0

#define RT946X_MASK_CHG_STAT	(0x3 << RT946X_SHIFT_CHG_STAT)
#define RT946X_MASK_ADC_STAT	(1 << RT946X_SHIFT_ADC_STAT)

/* ========== CHGNTC 0x43 ============ */
#define RT946X_SHIFT_BATNTC_FAULT	4

#define RT946X_MASK_BATNTC_FAULT	0x70

/* ========== CHGSTATC 0x50 ============ */
#define RT946X_SHIFT_PWR_RDY    7

#define RT946X_MASK_PWR_RDY     (1 << RT946X_SHIFT_PWR_RDY)

/* ========== CHGFAULT 0x51 ============ */
#define RT946X_SHIFT_CHG_VSYSUV	4
#define RT946X_SHIFT_CHG_VSYSOV	5
#define RT946X_SHIFT_CHG_VBATOV	6
#define RT946X_SHIFT_CHG_VBUSOV	7

#define RT946X_MASK_CHG_VSYSUV	(1 << RT946X_SHIFT_CHG_VSYSUV)
#define RT946X_MASK_CHG_VSYSOV	(1 << RT946X_SHIFT_CHG_VSYSOV)
#define RT946X_MASK_CHG_VBATOV	(1 << RT946X_SHIFT_CHG_VBATOV)
#define RT946X_MASK_CHG_VBUSOV	(1 << RT946X_SHIFT_CHG_VBUSOV)

/* ========== DPDMIRQ 0x56 ============ */
#ifdef CONFIG_CHARGER_RT9467
#define RT946X_SHIFT_DPDMIRQ_DETACH	1
#define RT946X_SHIFT_DPDMIRQ_ATTACH	0

#define RT946X_MASK_DPDMIRQ_DETACH	(1 << RT946X_SHIFT_DPDMIRQ_DETACH)
#define RT946X_MASK_DPDMIRQ_ATTACH	(1 << RT946X_SHIFT_DPDMIRQ_ATTACH)
#endif

/* ========== Variant-specific configuration ============ */
#if defined(CONFIG_CHARGER_RT9466)
	#define RT946X_CHARGER_NAME	"rt9466"
	#define RT946X_VENDOR_ID	0x80
	#define RT946X_ADDR		(0x53 << 1)
#elif defined(CONFIG_CHARGER_RT9467)
	#define RT946X_CHARGER_NAME	"rt9467"
	#define RT946X_VENDOR_ID	0x90
	#define RT946X_ADDR		(0x5B << 1)
#endif

/* RT946x specific interface functions */

/* Interrupt handler for rt946x */
void rt946x_interrupt(enum gpio_signal signal);

/* Enable/Disable rt946x (in charger or boost mode) */
int rt946x_enable_charger_boost(int en);

/*
 * Return 1 if VBUS is ready, which means
 * UVLO < VBUS < VOVP && VBUS > BATS + VSLP
 */
int rt946x_is_vbus_ready(void);

/* Return 1 if rt946x triggers charge termination due to full charge. */
int rt946x_is_charge_done(void);

/*
 * Cut off the battery (force BATFET to turn off).
 * Return 0 if it succeeds.
 */
int rt946x_cutoff_battery(void);

/* Enable/Disable charge temination */
int rt946x_enable_charge_termination(int en);

#endif /* __CROS_EC_RT946X_H */
