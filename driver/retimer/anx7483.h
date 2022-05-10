/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7483: Active redriver with linear equilzation
 */

#ifndef __CROS_EC_USB_RETIMER_ANX7483_H
#define __CROS_EC_USB_RETIMER_ANX7483_H

/*
 * Analog_Status_CTRL register
 *
 * 7:6 Reserved
 * 5   Reg_bypass_EN (0: Enable state machine, 1: Disable state machine.)
 * 4   Reg_EN (0: All configures are controlled by pin.
 *             1: All configures are controlled by registers.)
 * 3   Reserved
 * 2   FLIP_EN (0: no flip; 1: enable flip.)
 * 1   DP_EN (0: disable DP mode; 1: Enable DP mode.)
 * 0   USB_EN (1: disable USB mode; 1: enable USB mode.)
 */
#define ANX7483_ANALOG_STATUS_CTRL_REG	0x07
#define ANX7483_CTRL_REG_BYPASS_EN	BIT(5)
#define ANX7483_CTRL_REG_EN		BIT(4)
#define ANX7483_CTRL_FLIP_EN		BIT(2)
#define ANX7483_CTRL_DP_EN		BIT(1)
#define ANX7483_CTRL_USB_EN		BIT(0)

/*
 * Register_EQ/FG/SW_EN register
 * Enable equalizer/flat gain/output swing selection from pin or register
 *
 * 7:1 Reserved
 * 0   Reg_EQ/FG/SW_EN (0: from pin control; 1: from register control)
 */
#define ANX7483_ENABLE_EQ_FLAT_SWING_REG	0x15
#define ANX7483_ENABLE_EQ_FLAT_SWING_EN		BIT(0)

/*
 * EQ Settings Registers
 *
 * 7:4 Equilation settings when pin is input
 * 3:0 Fine tuning EQ step
 */
#define ANX7483_UTX1_PORT_CFG0_REG	0x52
#define ANX7483_UTX2_PORT_CFG0_REG	0x16
#define ANX7483_URX1_PORT_CFG0_REG	0x3E
#define ANX7483_URX2_PORT_CFG0_REG	0x2A
#define ANX7483_DRX1_PORT_CFG0_REG	0x5C
#define ANX7483_DRX2_PORT_CFG0_REG	0x20

/*
 * Default CFG0 value to apply: 9.2 dB with optimized tuning step
 */
#define ANX7483_CFG0_DEF		0x53

/*
 * Flat Gain Settings Registers
 *
 * 7:6 Fine tuning flat gain
 * 5:4 Flat gain settings when pin is input
 * 3:0 Fine tuning EQ
 */
#define ANX7483_UTX1_PORT_CFG2_REG	0x54
#define ANX7483_UTX2_PORT_CFG2_REG	0x18
#define ANX7483_URX1_PORT_CFG2_REG	0x40
#define ANX7483_URX2_PORT_CFG2_REG	0x2C
#define ANX7483_DRX1_PORT_CFG2_REG	0x5E
#define ANX7483_DRX2_PORT_CFG2_REG	0x22

/*
 * Default CFG2 value to apply: 0.3 dB with optimized fine tuning
 */
#define ANX7483_CFG2_DEF		0xEE

/*
 * Swing and 60K Input Termination Registers
 *
 * 7:6 Reserved
 * 4   Enable 60k input termination during reset or RX termination not powered
 * 3:2 Vendor internal use
 * 1:0 Swing setting when configured as input port
 */
#define ANX7483_UTX1_PORT_CFG4_REG	0x56
#define ANX7483_UTX2_PORT_CFG4_REG	0x1A
#define ANX7483_URX1_PORT_CFG4_REG	0x42
#define ANX7483_URX2_PORT_CFG4_REG	0x2E
#define ANX7483_DRX1_PORT_CFG4_REG	0x60
#define ANX7483_DRX2_PORT_CFG4_REG	0x24
#define ANX7483_DTX1_PORT_CFG4_REG	0x4C
#define ANX7483_DTX2_PORT_CFG4_REG	0x38

/*
 * Default values: 1300 mV gain with 60k termination either enabled or disabled
 */
#define ANX7483_CFG4_TERM_DISABLE	0x63
#define ANX7483_CFG4_TERM_ENABLE	0x73

/*
 * Termination Resistance Registers
 *
 * 7:3 Termination resistance selection (00111:100 ohms; 01111:90 ohms.)
 * 2   Termination resistance configuration for RX/TX mode (0: input, 1: output)
 * 1   Enable termination res for UTX2 path. (0:disable 1: enable.)
 * 0   Tune Flat Gain.
 */
#define ANX7483_UTX1_PORT_CFG3_REG	0x55
#define ANX7483_UTX2_PORT_CFG3_REG	0x19
#define ANX7483_URX1_PORT_CFG3_REG	0x41
#define ANX7483_URX2_PORT_CFG3_REG	0x2D
#define ANX7483_DTX1_PORT_CFG3_REG	0x4B
#define ANX7483_DTX2_PORT_CFG3_REG	0x37
#define ANX7483_DRX1_PORT_CFG3_REG	0x5F
#define ANX7483_DRX2_PORT_CFG3_REG	0x23

/*
 * Default values: Either 100Ohm or 90Ohm, input or output
 */
#define ANX7483_CFG3_100Ohm_IN		0x3A
#define ANX7483_CFG3_90Ohm_IN		0x7A
#define ANX7483_CFG3_90Ohm_OUT		0x7E

/*
 * AUX_Snooping_CTRL register
 *
 * 7:3 Reserved
 * 2:1 AUX_VTH (00:60mVppd, 01:90mVppd, 10:120mVppd, 11:140mVppd)
 * 0   AUX_Snooping_EN (0: disable; 1: enable.)
 */
#define ANX7483_AUX_SNOOPING_CTRL_REG	0x13

/*
 * Default value: Enable snooping with 90mVppd
 * (register ignored outside DP mode and does not need to be cleared)
 */
#define ANX7483_AUX_SNOOPING_DEF	0x13

/*
 * Middle Frequency Compensation
 *
 * 7:6 UTX1_EQ_CRT CTLE bias current when input
 * 5:3 UTX1_EQ_MFR CTLE middle-freq resistance when input
 * 2:0 UTX1_EQ_MFC CTLE middle-freq Capacitance
 */
#define ANX7483_UTX1_PORT_CFG1_REG	0x53
#define ANX7483_UTX2_PORT_CFG1_REG	0x17
#define ANX7483_URX1_PORT_CFG1_REG	0x3F
#define ANX7483_URX2_PORT_CFG1_REG	0x2B
#define ANX7483_DRX1_PORT_CFG1_REG	0x5D
#define ANX7483_DRX2_PORT_CFG1_REG	0x21

/*
 * Default CFG1 setting: current bias max, Middle frequency resistance of 0x5,
 * Middle frequency capacitance of 0x6
 */
#define ANX7483_CFG1_DEF		0xEE

#endif /* __CROS_EC_USB_RETIMER_ANX7483_H */
