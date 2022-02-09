/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Define power signals from device tree */

#ifndef __X86_POWER_SIGNALS_H__
#define __X86_POWER_SIGNALS_H__

#include <drivers/espi.h>
#include <drivers/gpio.h>

#if DT_HAS_COMPAT_STATUS_OKAY(intel_ap_pwrseq_signal_list)
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(intel_ap_pwrseq_signal_list) == 1,
	"Only one node for intel_ap_pwrseq_signal_list is allowed");
#endif

#define POWER_SIGNALS_LIST_NODE                                \
	DT_COMPAT_GET_ANY_STATUS_OKAY(intel_ap_pwrseq_signal_list)

#define POWER_SIGNAL_ENUM(id) DT_STRING_UPPER_TOKEN(id, pwrseq_signal_enum)
#define POWER_SIGNAL_ENUM_COMMA(id) POWER_SIGNAL_ENUM(id),

enum power_signal {
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_ENUM_COMMA)
	POWER_SIGNAL_COUNT
};

enum power_source {
	SOURCE_GPIO,
	SOURCE_VW,
	SOURCE_ADC,
	SOURCE_BOARD,
};

#define GPIO_PWR_ENUM(id, enum_name) DT_CAT(GPIO, enum_name)
#define GPIO_PWR_ENUM_COMMA(id, enum_name) GPIO_PWR_ENUM(id, enum_name),
#define GEN_GPIO_ENUM(id)                                      \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_int_gpios),    \
		(GPIO_PWR_ENUM_COMMA(id, POWER_SIGNAL_ENUM(id))), ())

enum gpio_power_signal {
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, GEN_GPIO_ENUM)
	POWER_SIGNAL_GPIO_COUNT
};

/* GPIO power signal configuration */
struct gpio_power_signal_config {
	enum power_signal power_signal;
	const struct gpio_dt_spec spec;
	gpio_flags_t intr_flags; /* GPIO interrupt flags */
	bool enable_on_boot;     /* Enable interrupt at boot up */
};

/* Power signal common configuration */
struct power_signal_config {
	const char *debug_name;
	enum power_source source;
	uint8_t source_id;
	uint8_t flags;
};

/* Power signal flags */
#define POWER_SIGNAL_ACTIVE_STATE BIT(0)
/* Convert enum power_signal to a mask for signal functions */
#define POWER_SIGNAL_MASK(signal) (1 << (signal))

#if defined(CONFIG_AP_X86_INTEL_ADL)

/* Input state flags */
#define IN_PCH_SLP_S0_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S0_DEASSERTED)
#define IN_PCH_SLP_S3_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_S5_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S5_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3 | \
				  IN_PCH_SLP_S4 | \
				  IN_PCH_SLP_SUS)
#define IN_PGOOD_ALL_CORE POWER_SIGNAL_MASK(X86_DSW_PWROK)
#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)
#define CHIPSET_G3S5_POWERUP_SIGNAL IN_PCH_SLP_SUS_DEASSERTED

#else
#warning("Input power signals state flags not defined");
#endif

#endif /* __X86_POWER_SIGNALS_H__ */
