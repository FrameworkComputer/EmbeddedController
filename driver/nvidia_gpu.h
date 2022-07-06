/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Nvidia GPU D-Notify driver header file
 */

#ifndef DRIVER_NVIDIA_GPU_H
#define DRIVER_NVIDIA_GPU_H

#define NVIDIA_GPU_ACOFF_DURATION (100 * MSEC)

enum d_notify_level {
	D_NOTIFY_1 = 0,
	D_NOTIFY_2,
	D_NOTIFY_3,
	D_NOTIFY_4,
	D_NOTIFY_5,
	D_NOTIFY_COUNT,
};

enum d_notify_policy_type {
	/* High- or low-power A/C */
	D_NOTIFY_AC,
	/* Too low of A/C to still charge or DC with high battery SOC */
	D_NOTIFY_AC_DC,
	/* DC with medium or low battery SOC */
	D_NOTIFY_DC,
};

struct d_notify_policy {
	enum d_notify_policy_type power_source;
	union {
		struct {
			unsigned int min_charger_watts;
		} ac;
		struct {
			unsigned int min_battery_soc;
		} dc;
	};
};

#define AC_ATLEAST_W(W)                                                   \
	{                                                                 \
		.power_source = D_NOTIFY_AC, .ac.min_charger_watts = (W), \
	}

#define AC_DC                                   \
	{                                       \
		.power_source = D_NOTIFY_AC_DC, \
	}

#define DC_ATLEAST_SOC(S)                                               \
	{                                                               \
		.power_source = D_NOTIFY_DC, .dc.min_battery_soc = (S), \
	}

void nvidia_gpu_init_policy(const struct d_notify_policy *policies);

/**
 * Notify the host of assertion or deassertion of GPU over temperature.
 *
 * @param assert  True for assert. False for deassert.
 */
void nvidia_gpu_over_temp(int assert);

#endif /* DRIVER_NVIDIA_GPU_H */
