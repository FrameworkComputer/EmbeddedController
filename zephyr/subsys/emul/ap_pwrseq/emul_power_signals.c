/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "chipset.h"
#include "emul/emul_power_signals.h"
#include "power_signals.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/espi_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(emul_power_signal, CONFIG_EMUL_POWER_SIGNALS_LOG_LEVEL);

/**
 * @brief Power signal source type.
 */
enum power_signal_emul_source {
	PWR_SIG_EMUL_SRC_GPIO,
	PWR_SIG_EMUL_SRC_VW,
	PWR_SIG_EMUL_SRC_EXT,
	PWR_SIG_EMUL_SRC_ADC,
};

struct wv_dt_spec {
	enum espi_vwire_signal espi_signal;
	bool invert;
};

/**
 * @brief Power signal containers definition.
 */
union power_signal_emul_signal_spec {
	struct gpio_dt_spec gpio;
	struct adc_dt_spec adc;
	struct wv_dt_spec vw;
};

/**
 * @brief Power signal descriptor.
 */
struct power_signal_emul_signal_desc {
	const enum power_signal enum_id;
	const char *name;
	const enum power_signal_emul_source source;
	const union power_signal_emul_signal_spec spec;
};

/**
 * @brief Power signal output definition.
 */
struct power_signal_emul_output {
	struct power_signal_emul_signal_desc desc;
	const int assert_value;
	const int assert_delay_ms;
	const int deassert_value;
	const int deassert_delay_ms;
	const int init_value;
	const bool initialized;
	const bool invert;
	struct k_work_delayable d_work;
	int value;
};

enum power_signal_edge {
	EDGE_ACTIVE_ON_ASSERT,
	EDGE_ACTIVE_ON_DEASSERT,
	EDGE_ACTIVE_ON_BOTH,
};

/**
 * @brief Power signal input definition.
 */
struct power_signal_emul_input {
	struct power_signal_emul_signal_desc desc;
	const int assert_value;
	const int init_value;
	const bool initialized;
	const enum power_signal_edge edge;
	struct gpio_callback cb;
	int value;
};

/**
 * @brief Power signal node definition,
 *        One node contains at least one input signal and one or more output
 *        signals.
 */
struct power_signal_emul_node {
	const char *name;
	struct power_signal_emul_input input;
	const int outputs_count;
	struct power_signal_emul_output *const outputs;
};

#define VW_DT_SPEC_GET(id)                                              \
	{                                                               \
		.espi_signal = DT_STRING_UPPER_TOKEN(id, virtual_wire), \
		.invert = DT_PROP(id, vw_invert),                       \
	}

#define EMUL_POWER_SIGNAL_GET_SOURCE(inst)                                    \
	COND_CODE_1(                                                          \
		DT_NODE_HAS_COMPAT(inst, intel_ap_pwrseq_gpio),               \
		(PWR_SIG_EMUL_SRC_GPIO),                                      \
		(COND_CODE_1(                                                 \
			DT_NODE_HAS_COMPAT(inst, intel_ap_pwrseq_vw),         \
			(PWR_SIG_EMUL_SRC_VW),                                \
			(COND_CODE_1(DT_NODE_HAS_COMPAT(                      \
					     inst, intel_ap_pwrseq_external), \
				     (PWR_SIG_EMUL_SRC_EXT),                  \
				     (PWR_SIG_EMUL_SRC_ADC))))))

#define EMUL_POWER_SIGNAL_GET_SIGNAL_SPEC(inst, dir_signal)                    \
	{                                                                      \
		COND_CODE_1(DT_NODE_HAS_COMPAT(DT_PROP(inst, dir_signal),      \
					       intel_ap_pwrseq_gpio),          \
			    (.gpio = GPIO_DT_SPEC_GET(                         \
				     DT_PROP(inst, dir_signal), gpios)),       \
			    (COND_CODE_1(DT_NODE_HAS_COMPAT(                   \
						 DT_PROP(inst, dir_signal),    \
						 intel_ap_pwrseq_vw),          \
					 (.vw = VW_DT_SPEC_GET(                \
						  DT_PROP(inst, dir_signal))), \
					 ())))                                 \
	}

#ifdef CONFIG_AP_PWRSEQ_SIGNAL_DEBUG_NAMES
#define ENUM_DBGNAME(inst, dir)         \
	"(" DT_PROP(DT_PROP(inst, dir), \
		    enum_name) ") " DT_PROP(DT_PROP(inst, dir), dbg_label)
#else
#define ENUM_DBGNAME(inst, dir) DT_PROP(DT_PROP(inst, dir), enum_name)
#endif

#define EMUL_POWER_SIGNAL_GET_SIGNAL(inst, dir)                             \
	{                                                                   \
		.enum_id = PWR_SIGNAL_ENUM(DT_PROP(inst, dir)),             \
		.name = ENUM_DBGNAME(inst, dir),                            \
		.source = EMUL_POWER_SIGNAL_GET_SOURCE(DT_PROP(inst, dir)), \
		.spec = EMUL_POWER_SIGNAL_GET_SIGNAL_SPEC(inst, dir),       \
	}

#define EMUL_POWER_SIGNAL_IN_DEF(inst)                                    \
	{                                                                 \
		.desc = EMUL_POWER_SIGNAL_GET_SIGNAL(inst, input_signal), \
		.assert_value = DT_PROP(inst, assert_value),              \
		.init_value = DT_PROP_OR(inst, init_value, 0),            \
		.edge = DT_STRING_TOKEN(inst, edge),                      \
		.initialized = DT_NODE_HAS_PROP(inst, init_value),        \
	}

#define EMUL_POWER_SIGNAL_OUT_DEF(inst)                                    \
	{                                                                  \
		.desc = EMUL_POWER_SIGNAL_GET_SIGNAL(inst, output_signal), \
		.assert_value = DT_PROP(inst, assert_value),               \
		.assert_delay_ms = DT_PROP(inst, assert_delay_ms),         \
		.deassert_value = DT_PROP(inst, deassert_value),           \
		.deassert_delay_ms = DT_PROP(inst, deassert_delay_ms),     \
		.init_value = DT_PROP_OR(inst, init_value, 0),             \
		.initialized = DT_NODE_HAS_PROP(inst, init_value),         \
		.invert = DT_PROP(inst, invert_value),                     \
	},

#define EMUL_POWER_SIGNAL_OUT_ARRAY_DEF(inst)                                 \
	static struct power_signal_emul_output DT_CAT(inst, _output)[] = {    \
		DT_FOREACH_CHILD_STATUS_OKAY(inst, EMUL_POWER_SIGNAL_OUT_DEF) \
	};

#define EMUL_POWER_SIGNAL_GET_INPUT_ENUM(inst) ENUM_##inst##_OUTPUT,

#define EMUL_POWER_SIGNAL_NODES_DEF(inst)                                      \
	enum {                                                                 \
		DT_FOREACH_CHILD_STATUS_OKAY(inst,                             \
					     EMUL_POWER_SIGNAL_GET_INPUT_ENUM) \
			DT_CAT(inst, _OUTPUT_COUNT),                           \
	};                                                                     \
	__COND_CODE(IS_EQ(DT_CAT(inst, _OUTPUT_COUNT), 0), (),                 \
		    (EMUL_POWER_SIGNAL_OUT_ARRAY_DEF(inst)))                   \
	static struct power_signal_emul_node DT_CAT(inst, _node) = {           \
		.name = DT_NODE_FULL_NAME(inst),                               \
		.input = EMUL_POWER_SIGNAL_IN_DEF(inst),                       \
		.outputs_count = inst##_OUTPUT_COUNT,                          \
		.outputs = __COND_CODE(IS_EQ(DT_CAT(inst, _OUTPUT_COUNT), 0),  \
				       (NULL), (DT_CAT(inst, _output))),       \
	};

DT_FOREACH_STATUS_OKAY(intel_ap_pwr_signal_emul, EMUL_POWER_SIGNAL_NODES_DEF)

#define EMUL_POWER_SIGNAL_NODES_ARRAY_GET_NODES_REFS_WITH_COMMA(inst) \
	(&DT_CAT(inst, _node)),

#define EMUL_POWER_SIGNAL_NODES_ARRAY_GET_NODES_REFS(inst) \
	EMUL_POWER_SIGNAL_NODES_ARRAY_GET_NODES_REFS_WITH_COMMA(inst)

#define EMUL_POWER_SIGNAL_NODES_ARRAY_GET_IO_SIGNALS(inst, prop, idx) \
	EMUL_POWER_SIGNAL_NODES_ARRAY_GET_NODES_REFS(                 \
		DT_PHANDLE_BY_IDX(inst, prop, idx))

#define EMUL_POWER_SIGNAL_NODES_ARRAY_DEF(inst)                          \
	static struct power_signal_emul_node *DT_CAT(inst, _nodes)[] = { \
		DT_FOREACH_PROP_ELEM(                                    \
			inst, nodes,                                     \
			EMUL_POWER_SIGNAL_NODES_ARRAY_GET_IO_SIGNALS)    \
	};

DT_FOREACH_STATUS_OKAY(intel_ap_pwr_test_platform,
		       EMUL_POWER_SIGNAL_NODES_ARRAY_DEF)

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_NODES_REFS_ITEM(inst) \
	DT_CAT(inst, _nodes),

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_NODES_REFS(inst) \
	EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_NODES_REFS_ITEM(inst)

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_IO_SIGNALS(inst, prop, idx) \
	EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_NODES_REFS(                 \
		DT_PHANDLE_BY_IDX(inst, prop, idx))

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_NODES(inst)                 \
	{                                                               \
		DT_FOREACH_PROP_ELEM(                                   \
			inst, nodes,                                    \
			EMUL_POWER_SIGNAL_TEST_PLATFORM_GET_IO_SIGNALS) \
	}

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_DEF(inst)             \
	const struct power_signal_emul_test_platform inst = { \
		.name_id = DT_NODE_FULL_NAME(inst),           \
		.nodes_count = DT_PROP_LEN(inst, nodes),      \
		.nodes = DT_CAT(inst, _nodes),                \
	};

DT_FOREACH_STATUS_OKAY(intel_ap_pwr_test_platform,
		       EMUL_POWER_SIGNAL_TEST_PLATFORM_DEF)

static K_KERNEL_STACK_DEFINE(work_q_stack,
			     CONFIG_EMUL_POWER_SIGNALS_WORK_QUEUE_STACK_SIZE);

struct k_work_q work_q;

static const struct power_signal_emul_test_platform *cur_test_platform;

static bool emul_ready;

/**
 * @brief Set GPIO type power signal to specified value.
 *
 * @param spec Pointer to container for GPIO pin information specified in
 *             devicetree.
 * @param value Value to be set on GPIO.
 */
static void power_signal_emul_set_gpio_value(const struct gpio_dt_spec *spec,
					     int value)
{
	__ASSERT(IS_ENABLED(CONFIG_AP_PWRSEQ_SIGNAL_GPIO),
		 "%s should only be used when GPIO power signals exist",
		 __func__);
#if CONFIG_AP_PWRSEQ_SIGNAL_GPIO
	gpio_flags_t gpio_flags;
	int ret;

	ret = gpio_emul_flags_get(spec->port, spec->pin, &gpio_flags);
	zassert_ok(ret, "Getting GPIO flags!!");

	if (gpio_flags & GPIO_INPUT) {
		ret = gpio_emul_input_set(
			spec->port, spec->pin,
			gpio_flags & GPIO_ACTIVE_LOW ? !value : !!value);
	} else if (gpio_flags & GPIO_OUTPUT) {
		ret = gpio_pin_set(spec->port, spec->pin, value);
	}
	zassert_ok(ret, "Setting GPIO value!!");
#endif
}

/**
 * @brief Get GPIO type power signal value.
 *
 * @param spec Pointer to container for GPIO pin information specified in
 *             devicetree.
 *
 * @return GPIO type power signal value.
 */
static int power_signal_emul_get_gpio_value(const struct gpio_dt_spec *spec)
{
	__ASSERT(IS_ENABLED(CONFIG_AP_PWRSEQ_SIGNAL_GPIO),
		 "%s should only be used when GPIO power signals exist",
		 __func__);
#if CONFIG_AP_PWRSEQ_SIGNAL_GPIO
	gpio_flags_t gpio_flags;
	int ret;

	ret = gpio_emul_flags_get(spec->port, spec->pin, &gpio_flags);
	zassert_ok(ret, "Getting GPIO flags!!");

	if (gpio_flags & GPIO_INPUT) {
		ret = gpio_pin_get(spec->port, spec->pin);
	} else if (gpio_flags & GPIO_OUTPUT) {
		ret = gpio_emul_output_get(spec->port, spec->pin);
		ret = gpio_flags & GPIO_ACTIVE_LOW ? !ret : !!ret;
	}

	return ret;
#else
	return 0;
#endif
}

/**
 * @brief Set virtual wire type power signal to value.
 *
 * @param spec Pointer to container for virtual wire information specified in
 *             devicetree.
 * @param value Value to be set on virtual wire.
 */
static void power_signal_emul_set_vw_value(const struct wv_dt_spec *vw,
					   int value)
{
	__ASSERT(IS_ENABLED(CONFIG_AP_PWRSEQ_SIGNAL_VW),
		 "%s should only be used when VW power signals exist",
		 __func__);
#if CONFIG_AP_PWRSEQ_SIGNAL_VW
	const struct device *espi =
		DEVICE_DT_GET_ANY(zephyr_espi_emul_controller);

	emul_espi_host_send_vw(espi, vw->espi_signal,
			       vw->invert ? !value : !!value);
#endif
}

/**
 * @brief Set power signal to specified value.
 *
 * @param desc Pointer to power signal descriptor.
 * @param value Value to be set on power signal.
 */
static void
power_signal_emul_set_value(struct power_signal_emul_signal_desc *desc,
			    int value)
{
	LOG_DBG("Set Signal %s -> %d", desc->name, value);

	switch (desc->source) {
	case PWR_SIG_EMUL_SRC_GPIO:
		power_signal_emul_set_gpio_value(&desc->spec.gpio, !!value);
		break;

	case PWR_SIG_EMUL_SRC_EXT:
		zassert_ok(power_signal_set(desc->enum_id, value),
			   "Setting %s Signal value!!", desc->name);
		break;

	case PWR_SIG_EMUL_SRC_VW:
		power_signal_emul_set_vw_value(&desc->spec.vw, value);
		break;

	default:
		zassert_unreachable("Undefined Signal %s!!", desc->name);
	}
	power_signal_interrupt(desc->enum_id, value);
}

/**
 * @brief Get power signal value.
 *
 * @param desc Pointer to power signal descriptor.
 *
 * @return Power signal value.
 */
static int
power_signal_emul_get_value(struct power_signal_emul_signal_desc *desc)
{
	int ret;

	switch (desc->source) {
	case PWR_SIG_EMUL_SRC_GPIO:
		ret = power_signal_emul_get_gpio_value(&desc->spec.gpio);
		break;

	case PWR_SIG_EMUL_SRC_VW:
		ret = power_signal_get(desc->enum_id);
		break;

	default:
		ret = power_signal_get(desc->enum_id);
		break;
	}

	return ret;
}

/**
 * @brief Handle GPIO type power signal interrupt.
 *
 * @param port Pointer to GPIO device.
 * @param cb Original struct gpio_callback owning this handler.
 * @param pins Mask of pins that triggers the callback handler.
 */
static void emul_power_signal_gpio_interrupt(const struct device *port,
					     struct gpio_callback *cb,
					     gpio_port_pins_t pins)
{
	struct power_signal_emul_input *in_signal =
		CONTAINER_OF(cb, struct power_signal_emul_input, cb);
	struct power_signal_emul_node *node =
		CONTAINER_OF(in_signal, struct power_signal_emul_node, input);
	int value;
	int delay;

	value = power_signal_emul_get_value(&in_signal->desc);
	if (value == in_signal->value) {
		return;
	}

	in_signal->value = value;

	if (!emul_ready) {
		return;
	}

	if (in_signal->edge == EDGE_ACTIVE_ON_DEASSERT &&
	    value == in_signal->assert_value) {
		return;
	} else if (in_signal->edge == EDGE_ACTIVE_ON_ASSERT &&
		   value != in_signal->assert_value) {
		return;
	}

	LOG_DBG("INT: Set Signal %s -> %d", in_signal->desc.name, value);
	for (int i = 0; i < node->outputs_count; i++) {
		struct power_signal_emul_output *out_signal = &node->outputs[i];

		out_signal->value = (value == in_signal->assert_value) ^
						    out_signal->invert ?
					    out_signal->assert_value :
					    out_signal->deassert_value;

		delay = (value == in_signal->assert_value) ?
				out_signal->assert_delay_ms :
				out_signal->deassert_delay_ms;

		LOG_DBG("INT: Delay Signal %s", out_signal->desc.name);
		k_work_schedule_for_queue(&work_q, &out_signal->d_work,
					  K_MSEC(delay));
	}
}

/**
 * @brief Handle power signal delayed work.
 *
 * This will set power signal value accordingly.
 *
 * @param work Pointer to work structure.
 */
static void emul_signal_work_hanlder(struct k_work *work)
{
	struct k_work_delayable *d_work = k_work_delayable_from_work(work);
	struct power_signal_emul_output *out_signal =
		CONTAINER_OF(d_work, struct power_signal_emul_output, d_work);

	power_signal_emul_set_value(&out_signal->desc, out_signal->value);
}

/**
 * @brief Initialize power signal emulator node.
 *
 * This will enable corresponding initiator power signal interruption and
 * its handler's power signals work structures.
 *
 * @param node Pointer to node containing power signals.
 */
static int power_signal_init_node(struct power_signal_emul_node *node)
{
	struct power_signal_emul_input *in_signal = &node->input;
	struct power_signal_emul_output *out_signal;

	if (!node->outputs_count) {
		LOG_ERR("Node does not have output signal!!");
		return -EINVAL;
	}

	LOG_DBG("Initializing node: %s", node->name);
	for (int i = 0; i < node->outputs_count; i++) {
		out_signal = &node->outputs[i];

		if (out_signal->initialized) {
			power_signal_emul_set_value(&out_signal->desc,
						    out_signal->init_value);
			out_signal->value = out_signal->init_value;
		} else {
			out_signal->value =
				power_signal_emul_get_value(&out_signal->desc);
		}
		k_work_init_delayable(&out_signal->d_work,
				      emul_signal_work_hanlder);
	}

	if (in_signal->initialized) {
		power_signal_emul_set_value(&in_signal->desc,
					    in_signal->init_value);
		in_signal->value = in_signal->init_value;
	} else {
		in_signal->value =
			power_signal_emul_get_value(&in_signal->desc);
	}
	if (in_signal->desc.source == PWR_SIG_EMUL_SRC_GPIO) {
		gpio_init_callback(&in_signal->cb,
				   emul_power_signal_gpio_interrupt,
				   BIT(in_signal->desc.spec.gpio.pin));

		gpio_add_callback(in_signal->desc.spec.gpio.port,
				  &in_signal->cb);

		gpio_pin_interrupt_configure_dt(&in_signal->desc.spec.gpio,
						GPIO_INT_EDGE_BOTH);
	}
	return 0;
}

/** See description in emul_power_signals.h */
int power_signal_emul_load(
	const struct power_signal_emul_test_platform *test_platform)
{
	int ret;

	if (cur_test_platform) {
		LOG_ERR("Power Signal Emulator Busy!!");
		return -EBUSY;
	}

	cur_test_platform = test_platform;

	LOG_DBG("Loading Emulator test: %s", cur_test_platform->name_id);

	for (int i = 0; i < cur_test_platform->nodes_count; i++) {
		ret = power_signal_init_node(cur_test_platform->nodes[i]);
		if (ret) {
			power_signal_emul_unload();
			return ret;
		}
	}

	emul_ready = true;
	LOG_DBG("Loading Emulator test Done");
	return 0;
}

/** See description in emul_power_signals.h */
int power_signal_emul_unload(void)
{
	struct power_signal_emul_node *node;
	struct power_signal_emul_output *out_signal;
	struct power_signal_emul_input *in_signal;

	if (!cur_test_platform) {
		LOG_ERR("No Test Platform Loaded!!");
		return -EINVAL;
	}

	emul_ready = false;
	for (int i = 0; i < cur_test_platform->nodes_count; i++) {
		node = cur_test_platform->nodes[i];
		in_signal = &node->input;

		if (in_signal->desc.source != PWR_SIG_EMUL_SRC_GPIO) {
			/* Currently, Only output GPIO signals are supported */
			continue;
		}

		for (int j = 0; j < node->outputs_count; j++) {
			static struct k_work_sync work_sync;

			out_signal = &node->outputs[j];
			k_work_cancel_delayable_sync(&out_signal->d_work,
						     &work_sync);
		}
		gpio_pin_interrupt_configure_dt(&in_signal->desc.spec.gpio,
						GPIO_INT_DISABLE);
		if (in_signal->cb.handler) {
			gpio_remove_callback(in_signal->desc.spec.gpio.port,
					     &in_signal->cb);
		}
	}
	cur_test_platform = NULL;
	return 0;
}

/**
 * @brief Initialize power signal emulator internal work queue.
 *
 * @param dev Unused parameter.
 *
 * @return 0 Return success only.
 */
static int power_signal_emul_work_q_init(void)
{
	struct k_work_queue_config cfg = {
		.name = "psignal_emul",
		.no_yield = true,
	};

	k_work_queue_start(&work_q, work_q_stack,
			   K_KERNEL_STACK_SIZEOF(work_q_stack),
			   CONFIG_EMUL_POWER_SIGNALS_WORK_QUEUE_PRIO, &cfg);
	return 0;
}

SYS_INIT(power_signal_emul_work_q_init, POST_KERNEL,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
