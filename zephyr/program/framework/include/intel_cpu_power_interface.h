/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INTEL_CPU_POWER_INTERFACE_H
#define __CROS_EC_INTEL_CPU_POWER_INTERFACE_H

#define POWER_LIMIT_1_W	30

/******************** Tau Value (TimeWindow)*************************************
 * 0.5sec: 0x1; 0.6sec : 0x52, 0.7sec: 0x92; 0.8sec : 0xD2
 * 1sec: 0x14;  1.25sec: 0x54; 1.5sec: 0x94; 1.75sec: 0xD4
 * 2sec: 0x16;  2.5sec : 0x56; 3sec  : 0x96; 3.5sec : 0xD6
 * 4sec: 0x18;  5sec   : 0x58; 6sec  : 0x98; 7sec   : 0xD8
 * 8sec: 0x1A;  10sec  : 0x5A; 12sec : 0x9A; 14sec  : 0xDA
 * 16sec: 0x1C; 20sec  : 0x5C; 24sec : 0x9C; 28sec  : 0xDC
 * 32sec: 0x1E; 40sec  : 0x5E; 48sec : 0x9E; 56sec  : 0xDE
 *******************************************************************************/
#define TIME_WINDOW_PL1		0xDC
#define TIME_WINDOW_PL2		0xDC

/******************** PL3 TimeWindow *******************************************
 * 1ms: 0x00;  1.25ms: 0x40;  1.5ms: 0xC0;  1.75ms: 0x80
 * 2ms: 0x02;  2.50ms: 0x42;  3ms: 0xC2;    3.50ms: 0x82
 * 4ms: 0x04;  5ms: 0x44;     6ms: 0xC4;    7ms: 0x84
 * 8ms: 0x06;  10ms: 0x46;    12ms: 0xC6;   14ms: 0x86
 * 16ms: 0x08; 20ms: 0x48;    24ms: 0xC8;   28ms: 0x88
 * 32ms: 0x0A; 40ms: 0x4A;    48ms: 0xCA;   56ms: 0x8A
 * 64ms: 0x0C;
 *******************************************************************************/
#define TIME_WINDOW_PL3		0xC8
#define DUTY_CYCLE_PL3		0x0A

/* RdPkgConfig and WrPkgConfig CPU Thermal and Power Optimiztion Services */
#define PECI_INDEX_POWER_LIMITS_PL1			0x1A
#define PECI_PARAMS_POWER_LIMITS_PL1			0x0000
#define PECI_PL1_CONTROL_TIME_WINDOWS(windows)		(windows << 16)
#define PECI_PL1_POWER_LIMIT_ENABLE(enable)		(enable << 15)
#define PECI_PL1_POWER_LIMIT(x)				(x << 3)

#define PECI_INDEX_POWER_LIMITS_PL2			0x1B
#define PECI_PARAMS_POWER_LIMITS_PL2			0x0000
#define PECI_PL2_CONTROL_TIME_WINDOWS(windows)		(windows << 16)
#define PECI_PL2_POWER_LIMIT_ENABLE(enable)		(enable << 15)
#define PECI_PL2_POWER_LIMIT(x)				(x << 3)

#define PECI_INDEX_POWER_LIMITS_PL3			0x39
#define PECI_PARAMS_POWER_LIMITS_PL3			0x0000
#define PECI_PL3_CONTROL_DUTY(duty)			(duty << 24)
#define PECI_PL3_CONTROL_TIME_WINDOWS(windows)		(windows << 16)
#define PECI_PL3_POWER_LIMIT_ENABLE(enable)		(enable << 15)
#define PECI_PL3_POWER_LIMIT(x)				(x << 3)

#define PECI_INDEX_POWER_LIMITS_PSYS_PL2		0x3B
#define PECI_PARAMS_POWER_LIMITS_PSYS_PL2		0x0000
#define PECI_PSYS_PL2_CONTROL_TIME_WINDOWS(windows)	(windows << 16) /* 28 seconds */
#define PECI_PSYS_PL2_POWER_LIMIT_ENABLE(enable)	(enable << 15)
#define PECI_PSYS_PL2_POWER_LIMIT(x)			(x << 3)

#define PECI_INDEX_POWER_LIMITS_PL4			0x3C
#define PECI_PARAMS_POWER_LIMITS_PL4			0x0000
#define PECI_PL4_POWER_LIMIT(x)				(x << 3)

extern int pl1_watt;
extern int pl2_watt;
extern int pl4_watt;
extern int pl3_watt;

int set_pl_limits(int pl1, int pl2, int pl4);

#endif
