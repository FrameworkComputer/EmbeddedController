/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Compose power signals list from device tree */
#include <shell/shell.h>
#include <stdlib.h>
#include <x86_power_signals.h>

#define GEN_ENABLE_ON_BOOT_DATA(id)                            \
	COND_CODE_1(DT_PROP(id, enable_int_on_boot), (1), (0))

#define POWER_SIGNAL_CONFIG_ENTRY(id, src, src_id)             \
[POWER_SIGNAL_ENUM(id)] =                                      \
{                                                              \
	.source = src,                                         \
	.debug_name = DT_PROP(id, dbg_label),                  \
	.source_id = src_id,                                   \
	.flags = DT_PROP(id, flags),                           \
},

#define GEN_BOARD_ENTRY(id) \
	POWER_SIGNAL_CONFIG_ENTRY(id, SOURCE_BOARD,            \
		DT_STRING_UPPER_TOKEN(id, pwrseq_board_value))

#define POWER_SIGNAL_BOARD_ENTRY(id)                           \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_board_value),  \
		(GEN_BOARD_ENTRY(id)), ())

#define GEN_ADC_ENTRY(id) \
	POWER_SIGNAL_CONFIG_ENTRY(id, SOURCE_ADC,              \
		DT_STRING_UPPER_TOKEN(id, pwrseq_adc_enum))

#define POWER_SIGNAL_ADC_ENTRY(id)                             \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_adc_enum),     \
		(GEN_ADC_ENTRY(id)), ())

#define GEN_VW_ENTRY(id)                                       \
	POWER_SIGNAL_CONFIG_ENTRY(id, SOURCE_VW,               \
		DT_STRING_UPPER_TOKEN(id, pwrseq_vw_enum))

#define POWER_SIGNAL_VW_ENTRY(id)                              \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_vw_enum),      \
		(GEN_VW_ENTRY(id)), ())

#define GEN_GPIO_ENTRY(id)                                     \
	POWER_SIGNAL_CONFIG_ENTRY(id, SOURCE_GPIO,             \
		GPIO_PWR_ENUM(id, POWER_SIGNAL_ENUM(id)))

#define POWER_SIGNAL_GPIO_ENTRY(id)                            \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_int_gpios),    \
		(GEN_GPIO_ENTRY(id)), ())

const struct power_signal_config power_signal_list[] = {
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_GPIO_ENTRY)
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_VW_ENTRY)
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_ADC_ENTRY)
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_BOARD_ENTRY)
};

#define GEN_GPIO_CONFIG_ENTRY(id)                              \
[GPIO_PWR_ENUM(id, POWER_SIGNAL_ENUM(id))] =                   \
{                                                              \
	.spec = GPIO_DT_SPEC_GET(id, pwrseq_int_gpios),        \
	.intr_flags = DT_PROP(id, pwrseq_int_flags),           \
	.enable_on_boot = GEN_ENABLE_ON_BOOT_DATA(id),         \
	.power_signal = POWER_SIGNAL_ENUM(id),                 \
},

#define POWER_SIGNAL_GPIO_CONFIG(id)                           \
	COND_CODE_1(DT_NODE_HAS_PROP(id, pwrseq_int_gpios),    \
		(GEN_GPIO_CONFIG_ENTRY(id)), ())

const struct gpio_power_signal_config power_signal_gpio_list[] = {
	DT_FOREACH_CHILD(POWER_SIGNALS_LIST_NODE, POWER_SIGNAL_GPIO_CONFIG)
};

struct gpio_callback intr_callbacks[POWER_SIGNAL_GPIO_COUNT];
