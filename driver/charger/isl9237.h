/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ISL-9237 battery charger driver.
 */

#ifndef __CROS_EC_ISL9237_H
#define __CROS_EC_ISL9237_H

#define ISL9237_ADDR 0x12 /* 7bit address 0001001 */

/* Registers */
#define ISL9237_REG_CHG_CURRENT      0x14
#define ISL9237_REG_ADAPTER_CURRENT1 0x3f
#define ISL9237_REG_ADAPTER_CURRENT2 0x3b
#define ISL9237_REG_SYS_VOLTAGE_MAX  0x15
#define ISL9237_REG_SYS_VOLTAGE_MIN  0x3e
#define ISL9237_REG_PROCHOT_AC       0x47
#define ISL9237_REG_PROCHOT_DC       0x48
#define ISL9237_REG_T1_T2            0x38
#define ISL9237_REG_CONTROL1         0x3c
#define ISL9237_REG_CONTROL2         0x3d
#define ISL9237_REG_INFO             0x3a
#define ISL9237_REG_OTG_VOLTAGE      0x49
#define ISL9237_REG_OTG_CURRENT      0x4a
#define ISL9237_REG_MANUFACTURER_ID  0xfe
#define ISL9237_REG_DEVICE_ID        0xff
#define ISL9237_REG_CONTROL0         0x39

/* Sense resistor default values in mOhm */
#define ISL9237_DEFAULT_SENSE_RESISTOR_AC 20
#define ISL9237_DEFAULT_SENSE_RESISTOR 10

/* Maximum charging current register value */
#define ISL9237_CURRENT_REG_MAX 0x17c0 /* bit<12:2> 10111110000 */

/* 2-level adpater current limit duration T1 & T2 in micro seconds */
#define ISL9237_T1_10000 0x00
#define ISL9237_T1_20000 0x01
#define ISL9237_T1_15000 0x02
#define ISL9237_T1_5000  0x03
#define ISL9237_T1_1000  0x04
#define ISL9237_T1_500   0x05
#define ISL9237_T1_100   0x06
#define ISL9237_T1_0     0x07
#define ISL9237_T2_10    (0x00 << 8)
#define ISL9237_T2_100   (0x01 << 8)
#define ISL9237_T2_500   (0x02 << 8)
#define ISL9237_T2_1000  (0x03 << 8)
#define ISL9237_T2_300   (0x04 << 8)
#define ISL9237_T2_750   (0x05 << 8)
#define ISL9237_T2_2000  (0x06 << 8)
#define ISL9237_T2_10000 (0x07 << 8)

#define ISL9237_SYS_VOLTAGE_REG_MAX 13824
#define ISL9237_SYS_VOLTAGE_REG_MIN 2048

/* PROCHOT# debounce time and duration time in micro seconds */
#define ISL9237_PROCHOT_DURATION_10000  (0 << 6)
#define ISL9237_PROCHOT_DURATION_20000  (1 << 6)
#define ISL9237_PROCHOT_DURATION_15000  (2 << 6)
#define ISL9237_PROCHOT_DURATION_5000   (3 << 6)
#define ISL9237_PROCHOT_DURATION_1000   (4 << 6)
#define ISL9237_PROCHOT_DURATION_500    (5 << 6)
#define ISL9237_PROCHOT_DURATION_100000 (6 << 6)
#define ISL9237_PROCHOT_DURATION_0      (7 << 6)
#define ISL9237_PROCHOT_DURATION_MASK   (7 << 6)

#define ISL9237_PROCHOT_DEBOUNCE_10   (0 << 9)
#define ISL9237_PROCHOT_DEBOUNCE_100  (1 << 9)
#define ISL9237_PROCHOT_DEBOUNCE_500  (2 << 9)
#define ISL9237_PROCHOT_DEBOUNCE_1000 (3 << 9)
#define ISL9237_PROCHOT_DEBOUNCE_MASK (3 << 9)

/* Maximum PROCHOT register value */
#define ISL9237_PROCHOT_AC_REG_MAX 6400
#define ISL9237_PROCHOT_DC_REG_MAX 12800

/* Control0: adapter voltage regulation reference */
#define ISL9237_C0_VREG_REF_3900 0
#define ISL9237_C0_VREG_REF_4200 1
#define ISL9237_C0_VREG_REF_4500 2
#define ISL9237_C0_VREG_REF_4800 3
#define ISL9237_C0_VREG_REF_MASK 0x03

/* Control0: disable adapter voltaqe regulation */
#define ISL9237_C0_DISABLE_VREG (1 << 2)

/* Control0: battery DCHOT reference for RS2 == 20mOhm */
#define ISL9237_C0_DCHOT_6A   (0 << 3)
#define ISL9237_C0_DCHOT_5A   (1 << 3)
#define ISL9237_C0_DCHOT_4A   (2 << 3)
#define ISL9237_C0_DCHOT_3A   (3 << 3)
#define ISL9237_C0_DCHOT_MASK (3 << 3)

/* Control1: general purpose comparator debounce time in micro second */
#define ISL9237_C1_GP_DEBOUNCE_2       (0 << 14)
#define ISL9237_C1_GP_DEBOUNCE_12      (1 << 14)
#define ISL9237_C1_GP_DEBOUNCE_2000    (2 << 14)
#define ISL9237_C1_GP_DEBOUNCE_5000000 (3 << 14)
#define ISL9237_C1_GP_DEBOUNCE_MASK    (3 << 14)

/* Control1: learn mode */
#define ISL9237_C1_LEARN_MODE_AUTOEXIT (1 << 13)
#define ISL9237_C1_LEARN_MODE_ENABLE   (1 << 12)

/* Control1: OTG enable */
#define ISL9237_C1_OTG (1 << 11)

/* Control1: audio filter */
#define ISL9237_C1_AUDIO_FILTER (1 << 10)

/* Control1: switch frequency */
#define ISL9237_C1_SWITCH_FREQ_PROG (0 << 7) /* 1000kHz or PROG */
#define ISL9237_C1_SWITCH_FREQ_913K (1 << 7)
#define ISL9237_C1_SWITCH_FREQ_839K (2 << 7)
#define ISL9237_C1_SWITCH_FREQ_777K (3 << 7)
#define ISL9237_C1_SWITCH_FREQ_723K (4 << 7)
#define ISL9237_C1_SWITCH_FREQ_676K (5 << 7)
#define ISL9237_C1_SWITCH_FREQ_635K (6 << 7)
#define ISL9237_C1_SWITCH_FREQ_599K (7 << 7)
#define ISL9237_C1_SWITCH_FREQ_MASK (7 << 7)

/* Control1: turbo mode */
#define ISL9237_C1_TURBO_MODE (1 << 6)

/* Control1: AMON & BMON */
#define ISL9237_C1_DISABLE_MON (1 << 5)
#define ISL9237_C1_SELECT_BMON (1 << 4)

/* Control1: PSYS, VSYS, VSYSLO */
#define ISL9237_C1_ENABLE_PSYS (1 << 3)
#define ISL9237_C1_ENABLE_VSYS (1 << 2)
#define ISL9237_C1_VSYSLO_REF_6000 0
#define ISL9237_C1_VSYSLO_REF_6300 1
#define ISL9237_C1_VSYSLO_REF_6600 2
#define ISL9237_C1_VSYSLO_REF_6900 3
#define ISL9237_C1_VSYSLO_REF_MASK 3

/* Control2: trickle charging current in mA */
#define ISL9237_C2_TRICKLE_256  (0 << 14)
#define ISL9237_C2_TRICKLE_128  (1 << 14)
#define ISL9237_C2_TRICKLE_64   (2 << 14)
#define ISL9237_C2_TRICKLE_512  (3 << 14)
#define ISL9237_C2_TRICKLE_MASK (3 << 14)

/* Control2: OTGEN debounce time in ms */
#define ISL9237_C2_OTG_DEBOUNCE_1300 (0 << 13)
#define ISL9237_C2_OTG_DEBOUNCE_150  (1 << 13)
#define ISL9237_C2_OTG_DEBOUNCE_MASK (1 << 13)

/* Control2: 2-level adapter over current */
#define ISL9237_C2_2LVL_OVERCURRENT (1 << 12)

/* Control2: adapter insertion debounce time in ms */
#define ISL9237_C2_ADAPTER_DEBOUNCE_1300 (0 << 11)
#define ISL9237_C2_ADAPTER_DEBOUNCE_150  (1 << 11)
#define ISL9237_C2_ADAPTER_DEBOUNCE_MASK (1 << 11)

/* Control2: PROCHOT debounce time in uS */
#define ISL9237_C2_PROCHOT_DEBOUNCE_10   (0 << 9)
#define ISL9237_C2_PROCHOT_DEBOUNCE_100  (1 << 9)
#define ISL9237_C2_PROCHOT_DEBOUNCE_500  (2 << 9)
#define ISL9237_C2_PROCHOT_DEBOUNCE_1000 (3 << 9)
#define ISL9237_C2_PROCHOT_DEBOUNCE_MASK (3 << 9)

/* Control2: min PROCHOT duration in uS */
#define ISL9237_C2_PROCHOT_DURATION_10000 (0 << 6)
#define ISL9237_C2_PROCHOT_DURATION_20000 (1 << 6)
#define ISL9237_C2_PROCHOT_DURATION_15000 (2 << 6)
#define ISL9237_C2_PROCHOT_DURATION_5000  (3 << 6)
#define ISL9237_C2_PROCHOT_DURATION_1000  (4 << 6)
#define ISL9237_C2_PROCHOT_DURATION_500   (5 << 6)
#define ISL9237_C2_PROCHOT_DURATION_100   (6 << 6)
#define ISL9237_C2_PROCHOT_DURATION_0     (7 << 6)
#define ISL9237_C2_PROCHOT_DURATION_MASK  (7 << 6)

/* Control2: turn off ASGATE in OTG mode */
#define ISL9237_C2_ASGATE_OFF (1 << 5)

/* Control2: CMIN, general purpose comparator reference in mV */
#define ISL9237_C2_CMIN_2000 (0 << 4)
#define ISL9237_C2_CMIN_1200 (1 << 4)

/* Control2: general purpose comparator enable */
#define ISL9237_C2_COMPARATOR (1 << 3)

/* Control2: invert CMOUT, general purpose comparator output, polarity */
#define ISL9237_C2_INVERT_CMOUT (1 << 2)

/* Control2: disable WOC, way over current */
#define ISL9237_C2_WOC_OFF (1 << 1)

/* Control2: PSYS gain in uA/W */
#define ISL9237_C2_PSYS_GAIN (1 << 0)

/* OTG voltage limit in mV, current limit in mA */
#define ISL9237_OTG_VOLTAGE_MIN 4864
#define ISL9237_OTG_VOLTAGE_MAX 5376
#define ISL9237_OTG_CURRENT_MAX 4096

/* Info register fields */
#define ISL9237_INFO_PROG_RESISTOR_MASK 0xf
#define ISL9237_INFO_TRICKLE_ACTIVE_MASK (1 << 4)
#define ISL9237_INFO_PSTATE_SHIFT 5
#define ISL9237_INFO_PSTATE_MASK 3

enum isl9237_power_stage {
	BUCK_MODE,
	BOOST_MODE,
	BUCK_BOOST_MODE,
	REVERSE_BUCK_MODE
};

#define ISL9237_INFO_FSM_STATE_SHIFT 7
#define ISL9237_INFO_FSM_STATE_MASK 7

enum isl9237_fsm_state {
	FSM_OFF,
	FSM_BAT,
	FSM_ADPT,
	FSM_ACOK,
	FSM_VSYS,
	FSM_CHRG,
	FSM_ENTOG,
	FSM_OTG
};

#define ISL9237_INFO_VSYSLO (1 << 10)
#define ISL9237_INFO_DCHOT (1 << 11)
#define ISL9237_INFO_ACHOT (1 << 12)


#define CHARGER_NAME  "isl9237"
#define CHARGE_V_MAX  ISL9237_SYS_VOLTAGE_REG_MAX
#define CHARGE_V_MIN  ISL9237_SYS_VOLTAGE_REG_MIN
#define CHARGE_V_STEP 8
#define CHARGE_I_MAX  ISL9237_CURRENT_REG_MAX
#define CHARGE_I_MIN  4
#define CHARGE_I_OFF  0
#define CHARGE_I_STEP 4
#define INPUT_I_MAX   ISL9237_CURRENT_REG_MAX
#define INPUT_I_MIN   4
#define INPUT_I_STEP  4

#define I2C_ADDR_CHARGER ISL9237_ADDR
#endif /* __CROS_EC_ISL9237_H */
