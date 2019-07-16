/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MAX17055.
 */

#ifndef __CROS_EC_MAX17055_H
#define __CROS_EC_MAX17055_H

#define MAX17055_ADDR_FLAGS         0x36
#define MAX17055_DEVICE_ID          0x4010
#define MAX17055_OCV_TABLE_SIZE     48

#define REG_STATUS                  0x00
#define REG_VALRTTH                 0x01
#define REG_TALRTTH                 0x02
#define REG_SALRTTH                 0x03
#define REG_AT_RATE                 0x04
#define REG_REMAINING_CAPACITY      0x05
#define REG_STATE_OF_CHARGE         0x06
#define REG_TEMPERATURE             0x08
#define REG_VOLTAGE                 0x09
#define REG_CURRENT                 0x0a
#define REG_AVERAGE_CURRENT         0x0b
#define REG_MIXCAP                  0x0f
#define REG_FULL_CHARGE_CAPACITY    0x10
#define REG_TIME_TO_EMPTY           0x11
#define REG_QR_TABLE00              0x12
#define REG_CONFIG                  0x1D
#define REG_AVERAGE_TEMPERATURE     0x16
#define REG_CYCLE_COUNT             0x17
#define REG_DESIGN_CAPACITY         0x18
#define REG_AVERAGE_VOLTAGE         0x19
#define REG_MAX_MIN_TEMP            0x1a
#define REG_MAX_MIN_VOLT            0x1b
#define REG_MAX_MIN_CURR            0x1c
#define REG_CHARGE_TERM_CURRENT     0x1e
#define REG_TIME_TO_FULL            0x20
#define REG_DEVICE_NAME             0x21
#define REG_QR_TABLE10              0x22
#define REG_FULLCAPNOM              0x23
#define REG_LEARNCFG                0x28
#define REG_QR_TABLE20              0x32
#define REG_RCOMP0                  0x38
#define REG_TEMPCO                  0x39
#define REG_EMPTY_VOLTAGE           0x3a
#define REG_FSTAT                   0x3d
#define REG_TIMER                   0x3e
#define REG_QR_TABLE30              0x42
#define REG_DQACC                   0x45
#define REG_DPACC                   0x46
#define REG_VFSOC0                  0x48
#define REG_COMMAND                 0x60
#define REG_LOCK1                   0x62
#define REG_LOCK2                   0x63
#define REG_OCV_TABLE_START         0x80
#define REG_STATUS2                 0xb0
#define REG_IALRTTH                 0xb4
#define REG_HIBCFG                  0xba
#define REG_CONFIG2                 0xbb
#define REG_TIMERH                  0xbe
#define REG_MODELCFG                0xdb
#define REG_VFSOC                   0xff

/* Status reg (0x00) flags */
#define STATUS_POR                  BIT(1)
#define STATUS_IMN                  BIT(2)
#define STATUS_BST                  BIT(3)
#define STATUS_IMX                  BIT(6)
#define STATUS_VMN                  BIT(8)
#define STATUS_TMN                  BIT(9)
#define STATUS_SMN                  BIT(10)
#define STATUS_VMX                  BIT(12)
#define STATUS_TMX                  BIT(13)
#define STATUS_SMX                  BIT(14)
#define STATUS_ALL_ALRT                                                        \
	(STATUS_IMN | STATUS_IMX | STATUS_VMN | STATUS_VMX | STATUS_TMN |      \
	 STATUS_TMX | STATUS_SMN | STATUS_SMX)

/* Alert disable values (0x01, 0x02, 0x03, 0xb4) */
#define VALRT_DISABLE               0xff00
#define TALRT_DISABLE               0x7f80
#define SALRT_DISABLE               0xff00
#define IALRT_DISABLE               0x7f80

/* Config reg (0x1d) flags */
#define CONF_AEN                    BIT(2)
#define CONF_IS                     BIT(11)
#define CONF_VS                     BIT(12)
#define CONF_TS                     BIT(13)
#define CONF_SS                     BIT(14)
#define CONF_TSEL                   BIT(15)
#define CONF_ALL_STICKY             (CONF_IS | CONF_VS | CONF_TS | CONF_SS)

/* FStat reg (0x3d) flags */
#define FSTAT_DNR                   0x0001
#define FSTAT_FQ                    0x0080

/* Config2 reg (0xbb) flags */
#define CONFIG2_LDMDL               BIT(5)

/* ModelCfg reg (0xdb) flags */
#define MODELCFG_REFRESH            BIT(15)
#define MODELCFG_VCHG               BIT(10)

/* Smart battery status bits (sbs reg 0x16) */
#define BATTERY_DISCHARGING         0x40
#define BATTERY_FULLY_CHARGED       0x20

/*
 * Before we have the battery fully characterized, we use these macros to
 * convert basic battery parameters to max17055 reg values for ez config.
 */

/* Convert design capacity in mAh to max17055 0x18 reg value */
#define MAX17055_DESIGNCAP_REG(bat_cap_mah) \
	(bat_cap_mah * BATTERY_MAX17055_RSENSE / 5)
/* Convert charge termination current in mA to max17055 0x1e reg value */
#define MAX17055_ICHGTERM_REG(term_cur_ma) \
	(((term_cur_ma * BATTERY_MAX17055_RSENSE) << 4) / 25)
/*
 * This macro converts empty voltage target (VE) and recovery voltage (VR)
 * in mV to max17055 0x3a reg value. max17055 declares 0% (empty battery) at
 * VE. max17055 reenables empty detection when the cell voltage rises above VR.
 * VE ranges from 0 to 5110mV, and VR ranges from 0 to 5080mV.
 */
#define MAX17055_VEMPTY_REG(ve_mv, vr_mv) \
	(((ve_mv / 10) << 7) | (vr_mv / 40))

#define MAX17055_MAX_MIN_REG(mx, mn) ((((int16_t)(mx)) << 8) | ((mn)))
/* Converts voltages alert range for VALRTTH_REG */
#define MAX17055_VALRTTH_RESOLUTION 20
#define MAX17055_VALRTTH_REG(mx, mn)                                           \
	MAX17055_MAX_MIN_REG((uint8_t)(mx / MAX17055_VALRTTH_RESOLUTION),      \
			     (uint8_t)(mn / MAX17055_VALRTTH_RESOLUTION))
/* Converts temperature alert range for TALRTTH_REG */
#define MAX17055_TALRTTH_REG(mx, mn)                                           \
	MAX17055_MAX_MIN_REG((int8_t)(mx), (int8_t)(mn))
/* Converts state-of-charge alert range for SALRTTH_REG */
#define MAX17055_SALRTTH_REG(mx, mn)                                           \
	MAX17055_MAX_MIN_REG((uint8_t)(mx), (uint8_t)(mn))
/* Converts current alert range for IALRTTH_REG */
/* Current resolution: 0.4mV/RSENSE */
#define MAX17055_IALRTTH_MUL (10 * BATTERY_MAX17055_RSENSE)
#define MAX17055_IALRTTH_DIV 4
#define MAX17055_IALRTTH_REG(mx, mn)                                           \
	MAX17055_MAX_MIN_REG(                                                  \
		(int8_t)(mx * MAX17055_IALRTTH_MUL / MAX17055_IALRTTH_DIV),    \
		(int8_t)(mn * MAX17055_IALRTTH_MUL / MAX17055_IALRTTH_DIV))

/*
 * max17055 needs some special battery parameters for fuel gauge
 * learning algorithm. Maxim can help characterize the battery pack
 * to get a full parameter list. We create a data structure to store
 * the battery parameters in the format of max17055 register values.
 */
struct max17055_batt_profile {
	/* Design capacity of the cell (LSB = 5uVH / Rsense) */
	uint16_t design_cap;
	/* Charge termination current (LSB = 1.5625uV / Rsense) */
	uint16_t ichg_term;
	/* The combination of empty voltage target and recovery voltage */
	uint16_t v_empty_detect;

	/*
	 * The parameters below are used for advanced (non-EZ) config
	 * (dpacc, learn_cfg, tempco, qr_table00, qr_table10,
	 * qr_table20, and qr_table30)
	 */

	/* Change in battery SOC between relaxation points (LSB = pct / 16) */
	uint16_t dpacc;
	/* Magic cell tuning parameters */
	uint16_t learn_cfg;
	uint16_t rcomp0;
	uint16_t tempco;
	uint16_t qr_table00;
	uint16_t qr_table10;
	uint16_t qr_table20;
	uint16_t qr_table30;

	/*
	 * If is_ez_config is nonzero, we only use design_cap, ichg_term,
	 * and v_empty_detect to config max17055 (a.k.a. EZ-config).
	 */
	uint8_t is_ez_config;

	/* Used only for full model */
	const uint16_t *ocv_table;
};

/* Return the special battery parameters max17055 needs. */
const struct max17055_batt_profile *max17055_get_batt_profile(void);

#ifdef CONFIG_BATTERY_MAX17055_ALERT
/*
 * max17055 supports alert on voltage, current, state-of-charge, and
 * temperature.  To enable this feature, the information of the limit range is
 * needed.
 */
struct max17055_alert_profile {
	/*
	 * Sets voltage upper and lower limits that generate an alert if
	 * voltage is outside of the v_alert_mxmn range.
	 * The upper 8 bits set the maximum value and the lower 8 bits set the
	 * minimum value. Interrupt threshold limits are selectable with 20mV
	 * resolution.
	 * Use MAX17055_VALRTTH_REG(max, min) to setup the desired range,
	 * VALRT_DISABLE to disable the alert.
	 */
	const uint16_t v_alert_mxmn;
	/*
	 * Sets temperature upper and lower limits that generate an alert if
	 * temperature is outside of the t_alert_mxmn range.
	 * The upper 8 bits set the maximum value and the lower 8 bits set the
	 * minimum value. Interrupt threshold limits are stored in
	 * 2’s-complement format with 1°C resolution.
	 * Use MAX17055_TALRTTH_REG(max, min) to setup the desired range,
	 * TALRT_DISABLE to disable the alert.
	 */
	const uint16_t t_alert_mxmn;
	/*
	 * Sets reported state-of-charge upper and lower limits that generate
	 * an alert if SOC is outside of the s_alert_mxmn range.
	 * The upper 8 bits set the maximum value and the lower 8 bits set the
	 * minimum value. Interrupt threshold limits are configurable with 1%
	 * resolution.
	 * Use MAX17055_SALRTTH_REG(max, min) to setup the desired range,
	 * SALRT_DISABLE to disable the alert.
	 */
	const uint16_t s_alert_mxmn;
	/*
	 * Sets current upper and lower limits that generate an alert if
	 * current is outside of the i_alert_mxmn range.
	 * The upper 8 bits set the maximum value and the lower 8 bits set the
	 * minimum value. Interrupt threshold limits are selectable with
	 * 0.4mV/R SENSE resolution.
	 * Use MAX17055_IALRTTH_REG(max, min) to setup the desired range,
	 * IALRT_DISABLE to disable the alert.
	 */
	const uint16_t i_alert_mxmn;
};

/*
 * Return the battery/system's alert threshoulds that max17055 needs.
 */
const struct max17055_alert_profile *max17055_get_alert_profile(void);
#endif /* CONFIG_BATTERY_MAX17055_ALERT */
#endif /* __CROS_EC_MAX17055_H */
