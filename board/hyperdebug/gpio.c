/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug GPIO logic and console commands */

#include "atomic.h"
#include "builtin/assert.h"
#include "cmsis-dap.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "gpio_chip.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Size of buffer used for gpio monitoring. */
#define CYCLIC_BUFFER_SIZE 65536
/* Number of concurrent gpio monitoring operations supported. */
#define NUM_CYCLIC_BUFFERS 3

struct dac_t {
	uint32_t enable_mask;
	volatile uint32_t *data_register;
};

/* Sparse array of DAC capabilities for GPIO pins. */
const struct dac_t dac_channels[GPIO_COUNT] = {
	[GPIO_CN7_9] = { STM32_DAC_CR_EN1, &STM32_DAC_DHR12R1 },
	[GPIO_CN7_10] = { STM32_DAC_CR_EN2, &STM32_DAC_DHR12R2 },
};

/*
 * GPIO structure for keeping extra flags such as GPIO_OPEN_DRAIN, to be applied
 * whenever the pin is switched into "alternate" mode.
 */
struct gpio_alt_flags {
	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (multiple bits allowed) */
	uint32_t mask;

	/* Flags (GPIO_*; see above). */
	uint32_t flags;
};

/*
 * Construct the gpio_alt_flags array, this really is just a subset of the
 * columns in the gpio_alt_funcs array in common/gpio.c (which is not accessible
 * from here).  This array is used by extra_alternate_flags().
 */
#define ALTERNATE(pinmask, function, module, flagz) \
	{ GPIO_##pinmask, .flags = (flagz) },

static __const_data const struct gpio_alt_flags gpio_alt_flags[] = {
#include "gpio.wrap"
};
#undef ALTERNATE

/*
 * Which pin of the shield is the RESET signal, that should be pulled down if
 * the blue user button is pressed.
 */
int shield_reset_pin = GPIO_COUNT; /* "no pin" value */

/*
 * A cyclic buffer is used to record events (edges) of one or more GPIO
 * signals.  Each event records the time since the previous event, and the
 * signal that changed (the direction of change is not explicitly recorded).
 *
 * So conceptually the buffer entries are pairs of (diff: u64, signal_no: u8).
 * These entries are encoded as bytes in the following way: First the timestamp
 * diff is shifted left by signal_bits, and the signal_no value put into the
 * lower bits freed up this way.  Now we have a single u64, which often will be
 * a small value (or at least, when the edges happen rapidly, and the need to
 * store many of them the highest, then the u64 will be a small value).  This
 * u64 is then stored 7 bits at a time in successive bytes, with the most
 * significant bit indicating whether more bytes belong to the same entry.
 *
 * The chain of relative timestamps are resolved by keeping two absolute
 * timestamps: head_time is the time of the most recently inserted event, and is
 * accessed and updated only by the interrupt handler.  tail_time is the past
 * timestamp on which the diff of the oldest record in the buffer is based (the
 * timestamp of the last record to be removed from the buffer), it is accessed
 * and updated only from the non-interrupt code that removes records from the
 * buffer.
 *
 * In a similar fashion, the signal level is recorded "at both ends" in for each
 * monitored signal by head_level and tail_level, the former only accessed from
 * the interrupt handler, and the latter only accessed from non-interrupt code.
 */
struct cyclic_buffer_header_t {
	/* Time base that the oldest event is relative to. */
	timestamp_t tail_time;
	/* Time of the most recent event, updated from interrupt context. */
	volatile uint32_t head_time;
	/* Index at which new records are placed, updated from interrupt. */
	uint8_t *volatile head;
	/* Index of oldest record. */
	const uint8_t *tail;
	/*
	 * End of cyclic byte buffer. Here head and tail will wrap back to the
	 * first byte of data[].
	 */
	uint8_t *end;
	/* Sticky bit recording if buffer overrun occurred. */
	volatile uint8_t overrun;
	/* Number of signals being monitored in this buffer. */
	uint8_t num_signals;
	/* The number of bits required to represent 0..num_signals-1. */
	uint8_t signal_bits;
	/* Data contents */
	uint8_t data[] __attribute__((aligned(8)));

	/*
	 * WARNING: Any change to this struct must be accompanied by
	 * corresponding changes in gpio_edge.S.
	 */
};

/*
 * The STM32L5 has 16 edge detection circuits.  Each pin can only be used with
 * one of them.  That is, detector 0 can take its input from one of pins A0,
 * B0, C0, ..., while detector 1 can choose between A1, B1, etc.
 *
 * Information about the current use of each detection circuit is stored in 16
 * "slots" below.
 */
struct monitoring_slot_t {
	/* Link to buffer recording edges of this signal. */
	struct cyclic_buffer_header_t *buffer;
	uint32_t gpio_base;
	uint32_t gpio_pin_mask;
	/* EC enum id of the signal used by this detection slot. */
	int gpio_signal;
	/*
	 * Most recently recorded level of the signal. (0: low, gpio_pin_mask:
	 * high).
	 */
	volatile uint32_t head_level;
	/*
	 * Level as of the current oldest end (tail) of the recording. (0: low,
	 * gpio_pin_mask: high).
	 */
	uint32_t tail_level;
	/* The index of the signal as used in the recording buffer. */
	uint8_t signal_no;
	/*
	 * The array below will contain a copy of the interrupt handler code, to
	 * execute from SRAM for speed, as well as for the convenience of being
	 * able to access member variables above using pc-relative addressing.
	 */
	uint8_t code[224] __attribute__((aligned(8)));

	/*
	 * WARNING: Any change to this struct must be accompanied by
	 * corresponding changes in gpio_edge.S.
	 */
};
struct monitoring_slot_t monitoring_slots[16];

/*
 * Memory area used for allocation of cyclic buffers.
 */
uint8_t buffer_area[NUM_CYCLIC_BUFFERS]
		   [sizeof(struct cyclic_buffer_header_t) + CYCLIC_BUFFER_SIZE];

static struct cyclic_buffer_header_t *allocate_cyclic_buffer(size_t size)
{
	for (int i = 0; i < NUM_CYCLIC_BUFFERS; i++) {
		struct cyclic_buffer_header_t *res =
			(struct cyclic_buffer_header_t *)buffer_area[i];
		if (res->num_signals)
			continue;
		if (sizeof(struct cyclic_buffer_header_t) + size >
		    sizeof(buffer_area[i])) {
			/* Requested size exceeds the capacity of the area. */
			return NULL;
		}
		/* Will be overwritten with another non-zero value by caller */
		res->num_signals = 0xFF;
		return res;
	}
	/* No free buffers */
	return NULL;
}

static void free_cyclic_buffer(struct cyclic_buffer_header_t *buf)
{
	buf->num_signals = 0;
}

/*
 * Counts unacknowledged buffer overruns.  Whenever non-zero, the red LED
 * will flash.
 */
atomic_t num_cur_error_conditions;

/*
 * Counts the number of cyclic buffers currently in existence, the green LED
 * will flash whenever this is non-zero, indicating the monitoring activity.
 */
int num_cur_monitoring = 0;

__attribute__((noinline)) void overrun(struct monitoring_slot_t *slot)
{
	struct cyclic_buffer_header_t *buffer_header = slot->buffer;
	gpio_disable_interrupt(slot->gpio_signal);
	if (!buffer_header->overrun) {
		buffer_header->overrun = 1;
		atomic_add(&num_cur_error_conditions, 1);
	}
}

/*
 * This interrupt routine is called without the usual wrapper for handling task
 * re-scheduling upon entry and exit.  This gives lower latency, which is
 * critical when recording a sequence of GPIO edges from software as is done
 * here.  Task-related functions MUST NEVER be called from within this handler.
 */
void gpio_edge(enum gpio_signal signal)
{
}

void edge_int(void);
void edge_int_end(void); /* Not a real function */

struct replacement_instruction_t {
	uint32_t count;
	uint8_t *location, *location_end;
	uint8_t *table, *table_end;
};
extern struct replacement_instruction_t load_pin_mask_replacement;
extern struct replacement_instruction_t signal_no_replacement;
extern struct replacement_instruction_t signal_bits_replacement;

/*
 * The arm architecture recognizes the "thumb" 16-bit instruction set by setting
 * the least significant bit of the instruction pointer.  The code still is
 * stored in 16-bit instructions at even addresses, but all function pointers
 * has added one to the code addresses.  The below macros convert between data
 * pointers suitable for memcpy(), and code pointers suitable for jumping to.
 * (By clearing or setting the lowest bit.)
 */
#define THUMB_CODE_TO_DATA_PTR(P) ((uint8_t *)((size_t)(P) & ~1U))
#define DATA_TO_THUMB_CODE_PTR(P) ((void (*)(void))((size_t)(P) | 1U))

__attribute__((noinline)) void
replace(struct monitoring_slot_t *slot,
	const struct replacement_instruction_t *instr, size_t index)
{
	size_t instruction_offset =
		instr->location - THUMB_CODE_TO_DATA_PTR(&edge_int);
	size_t instruction_size = instr->location_end - instr->location;
	ASSERT(instr->table_end - instr->table ==
	       instr->count * instruction_size);
	ASSERT(index < instr->count);
	(void)instruction_offset;
	memcpy(slot->code + instruction_offset,
	       THUMB_CODE_TO_DATA_PTR(instr->table) + index * instruction_size,
	       instruction_size);
}

/*
 * Blue user button pressed, assert/deassert the user-specified reset signal.
 */
void user_button_edge(enum gpio_signal signal)
{
	int pressed = gpio_get_level(GPIO_NUCLEO_USER_BTN);
	if (shield_reset_pin < GPIO_COUNT)
		gpio_set_level(shield_reset_pin, !pressed); /* Active low */
}

#define GPIO_IRQ_HIGHEST_PRIORITY(no)                                     \
	const struct irq_priority __keep IRQ_PRIORITY(STM32_IRQ_EXTI##no) \
		__attribute__((section(                                   \
			".rodata.irqprio"))) = { STM32_IRQ_EXTI##no, 0 }

GPIO_IRQ_HIGHEST_PRIORITY(0);
GPIO_IRQ_HIGHEST_PRIORITY(1);
GPIO_IRQ_HIGHEST_PRIORITY(2);
GPIO_IRQ_HIGHEST_PRIORITY(3);
GPIO_IRQ_HIGHEST_PRIORITY(4);
GPIO_IRQ_HIGHEST_PRIORITY(5);
GPIO_IRQ_HIGHEST_PRIORITY(6);
GPIO_IRQ_HIGHEST_PRIORITY(7);
GPIO_IRQ_HIGHEST_PRIORITY(8);
GPIO_IRQ_HIGHEST_PRIORITY(9);
GPIO_IRQ_HIGHEST_PRIORITY(10);
GPIO_IRQ_HIGHEST_PRIORITY(11);
GPIO_IRQ_HIGHEST_PRIORITY(12);
GPIO_IRQ_HIGHEST_PRIORITY(13);
GPIO_IRQ_HIGHEST_PRIORITY(14);
GPIO_IRQ_HIGHEST_PRIORITY(15);

/* Usual vector table in flash memory. */
extern void (*vectors[125])(void);

/* Our copy of the vector table in a specially aligned SRAM section. */
__attribute((section(".bss.vector_table"))) void (*sram_vectors[125])(void);

#define CORTEX_VTABLE REG32(0xE000ED08)

static void board_gpio_init(void)
{
	size_t interrupt_handler_size = THUMB_CODE_TO_DATA_PTR(&edge_int_end) -
					THUMB_CODE_TO_DATA_PTR(&edge_int);
	ASSERT(interrupt_handler_size <= sizeof(monitoring_slots[0].code));

	/* Mark every slot as unused. */
	for (int i = 0; i < ARRAY_SIZE(monitoring_slots); i++)
		monitoring_slots[i].gpio_signal = GPIO_COUNT;

	/* Enable handling of the blue user button of Nucleo-L552ZE-Q. */
	gpio_clear_pending_interrupt(GPIO_NUCLEO_USER_BTN);
	gpio_enable_interrupt(GPIO_NUCLEO_USER_BTN);

	/*
	 * Make a copy of the flash vector table in SRAM, then modify
	 * GPIO-related entries of the SRAM version to bypass EC-RTOS for lower
	 * latency.  Leave the original flash table active for now, switching to
	 * the SRAM one only when actively performing gpio monitoring.  This
	 * allows the above handling of presses of the blue button to operate on
	 * the ordinary rails, as long as no gpio monitoring is active.  (Button
	 * presses will not be handled while gpio monitoring is ongoing.)
	 */
	memcpy(sram_vectors, vectors, sizeof(sram_vectors));
	for (int i = 0; i < 16; i++) {
		memcpy(monitoring_slots[i].code,
		       THUMB_CODE_TO_DATA_PTR(&edge_int),
		       interrupt_handler_size);
		replace(&monitoring_slots[i], &load_pin_mask_replacement, i);
		/*
		 * Update GPIO edge interrupt vector to point directly at
		 * gpio_interrupt(), thereby bypassing the scheduling wrapper of
		 * DECLARE_IRQ().
		 *
		 * This is safe because these interrupts do not cause any task
		 * to become runnable.
		 *
		 * Set low bit of address to indicate thumb instruction set.
		 */
		sram_vectors[16 + STM32_IRQ_EXTI0 + i] =
			DATA_TO_THUMB_CODE_PTR(&monitoring_slots[i].code);
	}
}
DECLARE_HOOK(HOOK_INIT, board_gpio_init, HOOK_PRIO_DEFAULT);

static void stop_all_gpio_monitoring(void)
{
	struct monitoring_slot_t *slot;
	struct cyclic_buffer_header_t *buffer_header;
	for (int i = 0; i < ARRAY_SIZE(monitoring_slots); i++) {
		slot = monitoring_slots + i;
		if (!slot->buffer)
			continue;

		/*
		 * Disable interrupts for all signals feeding into the same
		 * cyclic buffer, and clear `slot->buffer` to make sure they are
		 * not discovered by next iteration of the outer loop.
		 */
		buffer_header = slot->buffer;
		for (int j = i; j < ARRAY_SIZE(monitoring_slots); j++) {
			slot = monitoring_slots + j;
			if (slot->buffer != buffer_header)
				continue;
			gpio_disable_interrupt(slot->gpio_signal);
			slot->gpio_signal = GPIO_COUNT;
			slot->buffer = NULL;
		}
		/* Deallocate this one cyclic buffer. */
		num_cur_monitoring--;
		if (buffer_header->overrun)
			atomic_sub(&num_cur_error_conditions, 1);
		free_cyclic_buffer(buffer_header);
	}

	/* Ensure handling of the blue user button of Nucleo-L552ZE-Q is
	 * enabled. */
	CORTEX_VTABLE = (uint32_t)(vectors);
	gpio_clear_pending_interrupt(GPIO_NUCLEO_USER_BTN);
	gpio_enable_interrupt(GPIO_NUCLEO_USER_BTN);
}

/*
 * Return GPIO_OPEN_DRAIN or any other special flags to apply when the given
 * signal is in "alternate" mode.
 */
static uint32_t extra_alternate_flags(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	const struct gpio_alt_flags *af;

	/* Find the first ALTERNATE() declaration for the given pin. */
	for (af = gpio_alt_flags;
	     af < gpio_alt_flags + ARRAY_SIZE(gpio_alt_flags); af++) {
		if (af->port != g->port)
			continue;

		if (af->mask & g->mask) {
			return af->flags;
		}
	}

	/* No ALTERNATE() declaration mention the given pin. */
	return 0;
}

/**
 * Find a GPIO signal by name.
 *
 * This is copied from gpio.c unfortunately, as it is static over there.
 *
 * @param name		Signal name to find
 *
 * @return the signal index, or GPIO_COUNT if no match.
 */
enum gpio_signal gpio_find_by_name(const char *name)
{
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++)
		if (gpio_is_implemented(i) &&
		    !strcasecmp(name, gpio_get_name(i)))
			return i;

	return GPIO_COUNT;
}

/*
 * Set the mode of a GPIO pin: input/opendrain/pushpull/alternate.
 */
static int command_gpio_mode(int argc, const char **argv)
{
	int gpio;
	int flags;
	uint32_t dac_enable_value = STM32_DAC_CR;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = gpio_find_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags &= ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_ANALOG);
	dac_enable_value &= ~dac_channels[gpio].enable_mask;
	if (strcasecmp(argv[2], "input") == 0)
		flags |= GPIO_INPUT;
	else if (strcasecmp(argv[2], "opendrain") == 0)
		flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
	else if (strcasecmp(argv[2], "pushpull") == 0)
		flags |= GPIO_OUTPUT;
	else if (strcasecmp(argv[2], "adc") == 0)
		flags |= GPIO_ANALOG;
	else if (strcasecmp(argv[2], "dac") == 0) {
		if (dac_channels[gpio].enable_mask == 0) {
			ccprintf("Error: Pin does not support dac\n");
			return EC_ERROR_PARAM2;
		}
		dac_enable_value |= dac_channels[gpio].enable_mask;
		/* Disable digital output, when DAC is overriding. */
		flags |= GPIO_INPUT;
	} else if (strcasecmp(argv[2], "alternate") == 0)
		flags |= GPIO_ALTERNATE | extra_alternate_flags(gpio);
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	STM32_DAC_CR = dac_enable_value;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(
	gpiomode, command_gpio_mode,
	"name <input | opendrain | pushpull | adc | dac | alternate>",
	"Set a GPIO mode", CMD_FLAG_RESTRICTED);

/*
 * Set the weak pulling of a GPIO pin: up/down/none.
 */
static int command_gpio_pull_mode(int argc, const char **argv)
{
	int gpio;
	int flags;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = gpio_find_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags = flags & ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
	if (strcasecmp(argv[2], "none") == 0)
		;
	else if (strcasecmp(argv[2], "up") == 0)
		flags |= GPIO_PULL_UP;
	else if (strcasecmp(argv[2], "down") == 0)
		flags |= GPIO_PULL_DOWN;
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(gpiopullmode, command_gpio_pull_mode,
			      "name <none | up | down>",
			      "Set a GPIO weak pull mode", CMD_FLAG_RESTRICTED);

static int set_dac(int gpio, const char *value)
{
	int milli_volts;
	char *e;
	if (dac_channels[gpio].enable_mask == 0) {
		ccprintf("Error: Pin does not support dac\n");
		return EC_ERROR_PARAM6;
	}

	milli_volts = strtoi(value, &e, 0);
	if (*e)
		return EC_ERROR_PARAM6;

	if (milli_volts <= 0)
		*dac_channels[gpio].data_register = 0;
	else if (milli_volts >= 3300)
		*dac_channels[gpio].data_register = 4095;
	else
		*dac_channels[gpio].data_register = milli_volts * 4096 / 3300;

	return EC_SUCCESS;
}

/*
 * Set the value in millivolts for driving the DAC of a given pin.
 */
static int command_gpio_analog_set(int argc, const char **argv)
{
	int gpio;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	gpio = gpio_find_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;

	if (set_dac(gpio, argv[3]) != EC_SUCCESS)
		return EC_ERROR_PARAM3;
	return EC_SUCCESS;
}

/*
 * Set multiple aspects of a GPIO pin simultaneously, that is, can switch output
 * level, opendrain/pushpull, and pullup simultaneously, eliminating the risk of
 * glitches.
 */
static int command_gpio_multiset(int argc, const char **argv)
{
	int gpio;
	int flags;
	uint32_t dac_enable_value = STM32_DAC_CR;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	gpio = gpio_find_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;
	flags = gpio_get_flags(gpio);

	if (argc > 3 && strcasecmp(argv[3], "-") != 0) {
		flags = flags & ~(GPIO_LOW | GPIO_HIGH);
		if (strcasecmp(argv[3], "0") == 0)
			flags |= GPIO_LOW;
		else if (strcasecmp(argv[3], "1") == 0)
			flags |= GPIO_HIGH;
		else
			return EC_ERROR_PARAM3;
	}

	if (argc > 4 && strcasecmp(argv[4], "-") != 0) {
		flags &= ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OPEN_DRAIN |
			   GPIO_ANALOG);
		dac_enable_value &= ~dac_channels[gpio].enable_mask;
		if (strcasecmp(argv[4], "input") == 0)
			flags |= GPIO_INPUT;
		else if (strcasecmp(argv[4], "opendrain") == 0)
			flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
		else if (strcasecmp(argv[4], "pushpull") == 0)
			flags |= GPIO_OUTPUT;
		else if (strcasecmp(argv[4], "adc") == 0)
			flags |= GPIO_ANALOG;
		else if (strcasecmp(argv[4], "dac") == 0) {
			if (dac_channels[gpio].enable_mask == 0) {
				ccprintf("Error: Pin does not support dac\n");
				return EC_ERROR_PARAM2;
			}
			dac_enable_value |= dac_channels[gpio].enable_mask;
			/* Disable digital output, when DAC is overriding. */
			flags |= GPIO_INPUT;
		} else if (strcasecmp(argv[4], "alternate") == 0)
			flags |= GPIO_ALTERNATE | extra_alternate_flags(gpio);
		else
			return EC_ERROR_PARAM4;
	}

	if (argc > 5 && strcasecmp(argv[5], "-") != 0) {
		flags = flags & ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
		if (strcasecmp(argv[5], "none") == 0)
			;
		else if (strcasecmp(argv[5], "up") == 0)
			flags |= GPIO_PULL_UP;
		else if (strcasecmp(argv[5], "down") == 0)
			flags |= GPIO_PULL_DOWN;
		else
			return EC_ERROR_PARAM5;
	}

	if (argc > 6 && strcasecmp(argv[6], "-") != 0) {
		if (set_dac(gpio, argv[6]) != EC_SUCCESS)
			return EC_ERROR_PARAM6;
	}

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	STM32_DAC_CR = dac_enable_value;
	return EC_SUCCESS;
}

/*
 * Choose the pin that should be pulled low when the blue user button is
 * pressed.
 */
static int command_gpio_set_reset(int argc, const char **argv)
{
	int gpio;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[2], "none")) {
		shield_reset_pin = GPIO_COUNT; /* "no pin" value */
		return EC_SUCCESS;
	}

	gpio = gpio_find_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;

	shield_reset_pin = gpio;
	return EC_SUCCESS;
}

static int command_gpio_monitoring_start(int argc, const char **argv)
{
	BUILD_ASSERT(STM32_IRQ_EXTI15 < 32);
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	timestamp_t now;
	int rv;
	uint32_t nvic_mask;
	/* Maybe configurable by parameter */
	size_t cyclic_buffer_size = CYCLIC_BUFFER_SIZE;
	struct cyclic_buffer_header_t *buf;
	struct monitoring_slot_t *slot;

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = gpio_find_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT) {
			rv = EC_ERROR_PARAM3 + i;
			goto out_partial_cleanup;
		}
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != GPIO_COUNT) {
			ccprintf("Error: Monitoring of %s conflicts with %s\n",
				 argv[3 + i],
				 gpio_list[slot->gpio_signal].name);
			rv = EC_ERROR_PARAM3 + i;
			goto out_partial_cleanup;
		}
		slot->gpio_signal = gpios[i];
	}

	/*
	 * All the requested signals were available for monitoring, and their
	 * slots have been marked as reserved for the respective signal.
	 */
	buf = allocate_cyclic_buffer(cyclic_buffer_size);
	if (!buf) {
		rv = EC_ERROR_BUSY;
		goto out_cleanup;
	}

	/* Disable handling of the blue user button while monitoring is ongoing.
	 */
	if (!num_cur_monitoring) {
		gpio_disable_interrupt(GPIO_NUCLEO_USER_BTN);
		CORTEX_VTABLE = (uint32_t)(sram_vectors);
	}

	buf->tail = buf->head = buf->data;
	buf->end = buf->head + cyclic_buffer_size;
	buf->overrun = 0;
	buf->num_signals = gpio_num;
	buf->signal_bits = 0;
	/* Compute how many bits are required to represent 0..gpio_num-1. */
	while ((gpio_num - 1) >> buf->signal_bits)
		buf->signal_bits++;

	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot->gpio_base = gpio_list[gpios[i]].port;
		slot->gpio_pin_mask = gpio_list[gpios[i]].mask;
		slot->buffer = buf;
		slot->signal_no = i;
		replace(slot, &signal_no_replacement, i);
		replace(slot, &signal_bits_replacement, buf->signal_bits);
	}

	/*
	 * The code relies on all EXTIn interrupts belonging to the same 32-bit
	 * NVIC register, so that multiple interrupts can be "unleashed"
	 * simultaneously.
	 */
	nvic_mask = 0;

	/*
	 * Disable interrupts in GPIO/EXTI detection circuits (should be
	 * disabled already, but disabled and clear pending bit to be on the
	 * safe side).
	 */
	for (i = 0; i < gpio_num; i++) {
		int gpio_num = GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		gpio_disable_interrupt(gpios[i]);
		gpio_clear_pending_interrupt(gpios[i]);
		nvic_mask |= BIT(STM32_IRQ_EXTI0 + gpio_num);
	}
	/* Also disable interrupts at NVIC (interrupt controller) level. */
	CPU_NVIC_UNPEND(0) = nvic_mask;
	CPU_NVIC_DIS(0) = nvic_mask;

	for (i = 0; i < gpio_num; i++) {
		int gpio_num = GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot = monitoring_slots + gpio_num;
		/*
		 * Tell the GPIO block to start detecting rising and falling
		 * edges, and latch them in STM32_EXTI_RPR and STM32_EXTI_FPR
		 * respectively.  Interrupts are still disabled in the NVIC,
		 * meaning that the execution will not be interrupted, yet, even
		 * if the GPIO block requests interrupt.
		 */
		gpio_enable_interrupt(gpios[i]);
		slot->head_level = slot->tail_level =
			gpio_get_level(gpios[i]) ? gpio_list[gpios[i]].mask : 0;
		/*
		 * Race condition here!  If three or more edges happen in
		 * rapid succession, we may fail to record some of them, but
		 * we should never over-report edges.
		 *
		 * Since edge detection was enabled before the "head_level"
		 * was polled, if an edge happened between the two, then an
		 * interrupt is currently pending, and when handled after this
		 * loop, the logic in the gpio_edge interrupt handler would
		 * wrongly conclude that the signal must have seen two
		 * transitions, in order to end up at the same level as before.
		 * In order to avoid such over-reporting, we clear "pending"
		 * interrupt bit below, but only for the direction that goes
		 * "towards" the level measured above.
		 */
		if (slot->head_level)
			STM32_EXTI_RPR = BIT(gpio_num);
		else
			STM32_EXTI_FPR = BIT(gpio_num);
	}
	/*
	 * Now enable the handling of the set of interrupts.
	 */
	now = get_time();
	buf->head_time = now.le.lo;
	CPU_NVIC_EN(0) = nvic_mask;

	buf->tail_time = now;
	num_cur_monitoring++;
	ccprintf("  @%lld\n", buf->tail_time.val);

	/*
	 * Dump the initial level of each input, for the convenience of the
	 * caller.  (Allow makes monitoring useful, even if a signal has no
	 * transitions during the monitoring period.
	 */
	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		ccprintf("  %d %s %d\n", i, gpio_list[gpios[i]].name,
			 !!slot->tail_level);
	}

	return EC_SUCCESS;

out_cleanup:
	i = gpio_num;
out_partial_cleanup:
	while (i-- > 0) {
		monitoring_slots[GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask)]
			.gpio_signal = GPIO_COUNT;
	}
	return rv;
}

static const uint8_t *
traverse_buffer(struct cyclic_buffer_header_t *buf, int gpio_signals_by_no[],
		timestamp_t now, size_t limit,
		void (*process_event)(uint8_t, timestamp_t, bool));

static timestamp_t traverse_until;

static void print_event(uint8_t signal_no, timestamp_t event_time, bool rising)
{
	/* To conserve bandwidth, timestamps are relative to `traverse_until`.
	 */
	ccprintf("  %d %lld %s\n", signal_no,
		 event_time.val - traverse_until.val, rising ? "R" : "F");
	/* Flush console to avoid truncating output */
	cflush();
}

static int command_gpio_monitoring_read(int argc, const char **argv)
{
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	struct cyclic_buffer_header_t *buf = NULL;
	struct monitoring_slot_t *slot;
	int gpio_signals_by_no[16];

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = gpio_find_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT)
			return EC_ERROR_PARAM3 + i; /* May overflow */
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != gpios[i]) {
			ccprintf("Error: Not monitoring %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (slot->signal_no != i) {
			ccprintf("Error: Inconsistent order at %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (buf == NULL) {
			buf = slot->buffer;
		} else if (buf != slot->buffer) {
			ccprintf(
				"Error: Not monitoring %s as part of same groups as %s\n",
				gpio_list[gpios[i]].name,
				gpio_list[gpios[0]].name);
			return EC_ERROR_PARAM3 + i;
		}
		gpio_signals_by_no[slot->signal_no] = gpios[i];
	}
	if (gpio_num != buf->num_signals) {
		ccprintf("Error: Not full set of signals monitored\n");
		return EC_ERROR_INVAL;
	}

	/*
	 * Print at most 32 lines at a time, since `cflush()` seems to not
	 * prevent overflow.
	 */
	traverse_until = get_time();
	ccprintf("  @%lld\n", traverse_until.val);
	buf->tail = traverse_buffer(buf, gpio_signals_by_no, traverse_until, 32,
				    &print_event);
	if (buf->tail != buf->head)
		ccprintf("Warning: more data\n");
	if (buf->overrun)
		ccprintf("Error: Buffer overrun\n");
	return EC_SUCCESS;
}

/*
 * This routine iterates through buffered entries starting from buf->tail,
 * stopping when there are no more entries before the `now` timestamp, or when
 * having processed a certain number of entries given by `limit`, whichever
 * comes first.  The return value indicate a new value which the caller must put
 * into buf->tail.  As soon as the caller does this, the traversed range is free
 * to be overwritten by the interrupt handler.
 */
static const uint8_t *
traverse_buffer(struct cyclic_buffer_header_t *buf, int gpio_signals_by_no[],
		timestamp_t now, size_t limit,
		void (*process_event)(uint8_t, timestamp_t, bool))
{
	/*
	 * We have read the current time, before taking a snapshot of the head
	 * pointer as set by the interrupt handler.  This way, we can guarantee
	 * that the transcript will include any edge happening at or before the
	 * `now` timestamp.  If an interrupt happens after `now` was captured,
	 * but before the line below, causing our head pointer to include an
	 * event that happened after "now", then it and any further entries will
	 * be excluded from the traversal, and remain in the cyclic buffer for
	 * the next invocation of `gpio monitoring read`.
	 */
	const uint8_t *head = buf->head;

	int8_t signal_bits = buf->signal_bits;
	const uint8_t *tail = buf->tail;
	timestamp_t tail_time = buf->tail_time;
	while (tail != head && limit-- > 0) {
		const uint8_t *const buf_start = buf->data;
		timestamp_t diff;
		uint8_t byte;
		uint8_t signal_no;
		uint32_t mask;
		int shift = 0;
		const uint8_t *tentative_tail = tail;
		struct monitoring_slot_t *slot;
		diff.val = 0;
		do {
			byte = *tentative_tail++;
			if (tentative_tail == buf->end)
				tentative_tail = buf_start;
			diff.val |= (byte & 0x7F) << shift;
			shift += 7;
		} while (byte & 0x80);
		signal_no = diff.val & (0xFF >> (8 - signal_bits));
		diff.val >>= signal_bits;
		if (tail_time.val + diff.val > now.val) {
			/*
			 * Do not consume this or subsequent records, which
			 * apparently happened after our "now" timestamp from
			 * earlier in the execution of this method.
			 */
			break;
		}
		tail = tentative_tail;
		tail_time.val += diff.val;
		mask = gpio_list[gpio_signals_by_no[signal_no]].mask;
		slot = monitoring_slots + GPIO_MASK_TO_NUM(mask);
		slot->tail_level ^= mask;
		if (process_event)
			process_event(signal_no, tail_time, slot->tail_level);
	}
	buf->tail_time = tail_time;
	return tail;
}

static int command_gpio_monitoring_stop(int argc, const char **argv)
{
	int gpios[16];
	int gpio_num = argc - 3;
	int i;
	struct cyclic_buffer_header_t *buf = NULL;
	struct monitoring_slot_t *slot;

	if (gpio_num <= 0 || gpio_num > 16)
		return EC_ERROR_PARAM_COUNT;

	for (i = 0; i < gpio_num; i++) {
		gpios[i] = gpio_find_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT)
			return EC_ERROR_PARAM3 + i; /* May overflow */
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		if (slot->gpio_signal != gpios[i]) {
			ccprintf("Error: Not monitoring %s\n",
				 gpio_list[gpios[i]].name);
			return EC_ERROR_PARAM3 + i;
		}
		if (buf == NULL) {
			buf = slot->buffer;
		} else if (buf != slot->buffer) {
			ccprintf(
				"Error: Not monitoring %s as part of same groups as %s\n",
				gpio_list[gpios[i]].name,
				gpio_list[gpios[0]].name);
			return EC_ERROR_PARAM3 + i;
		}
	}
	if (gpio_num != buf->num_signals) {
		ccprintf("Error: Not full set of signals monitored\n");
		return EC_ERROR_INVAL;
	}

	for (i = 0; i < gpio_num; i++) {
		gpio_disable_interrupt(gpios[i]);
	}

	/*
	 * With no more interrupts modifying the buffer, it can be deallocated.
	 */
	num_cur_monitoring--;
	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		slot->gpio_signal = GPIO_COUNT;
		slot->buffer = NULL;
	}

	if (buf->overrun)
		atomic_sub(&num_cur_error_conditions, 1);

	/* Re-enable handling of the blue user button once monitoring is done.
	 */
	if (!num_cur_monitoring) {
		CORTEX_VTABLE = (uint32_t)(vectors);
		gpio_clear_pending_interrupt(GPIO_NUCLEO_USER_BTN);
		gpio_enable_interrupt(GPIO_NUCLEO_USER_BTN);
	}

	free_cyclic_buffer(buf);
	return EC_SUCCESS;
}

static int command_gpio_monitoring(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "start"))
		return command_gpio_monitoring_start(argc, argv);
	if (!strcasecmp(argv[2], "read"))
		return command_gpio_monitoring_read(argc, argv);
	if (!strcasecmp(argv[2], "stop"))
		return command_gpio_monitoring_stop(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_gpio(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "analog-set"))
		return command_gpio_analog_set(argc, argv);
	if (!strcasecmp(argv[1], "monitoring"))
		return command_gpio_monitoring(argc, argv);
	if (!strcasecmp(argv[1], "multiset"))
		return command_gpio_multiset(argc, argv);
	if (!strcasecmp(argv[1], "set-reset"))
		return command_gpio_set_reset(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(
	gpio, command_gpio,
	"multiset name [level] [mode] [pullmode] [milli_volts]"
	"\nanalog-set name milli_volts"
	"\nset-reset name"
	"\nmonitoring start name..."
	"\nmonitoring read name..."
	"\nmonitoring stop name...",
	"GPIO manipulation", CMD_FLAG_RESTRICTED);

static void gpio_reinit(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	stop_all_gpio_monitoring();

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		if (flags & GPIO_ALTERNATE)
			flags |= extra_alternate_flags(i);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}

	/* Disable any DAC (which would override GPIO function of pins) */
	STM32_DAC_CR = 0;

	/*
	 * Default behavior of blue user button is to pull CN10_29 low, as that
	 * pin is used for RESET on both OpenTitan shield and legacy GSC
	 * shields.
	 */
	shield_reset_pin = GPIO_CN10_29;
}
DECLARE_HOOK(HOOK_REINIT, gpio_reinit, HOOK_PRIO_DEFAULT);

static void led_tick(void)
{
	/* Indicate ongoing GPIO monitoring by flashing the green LED. */
	if (num_cur_monitoring) {
		gpio_set_level(GPIO_NUCLEO_LED1,
			       !gpio_get_level(GPIO_NUCLEO_LED1));
	} else {
		/*
		 * If not flashing, leave the green LED on, to indicate that
		 * HyperDebug firmware is running and ready.
		 */
		gpio_set_level(GPIO_NUCLEO_LED1, 1);
	}
	/* Indicate error conditions by flashing red LED. */
	if (atomic_add(&num_cur_error_conditions, 0))
		gpio_set_level(GPIO_NUCLEO_LED3,
			       !gpio_get_level(GPIO_NUCLEO_LED3));
	else {
		/*
		 * If not flashing, leave the red LED off.
		 */
		gpio_set_level(GPIO_NUCLEO_LED3, 0);
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

size_t queue_add_units(struct queue const *q, const void *src, size_t count);

static void queue_blocking_add(struct queue const *q, const void *src,
			       size_t count)
{
	while (true) {
		size_t progress = queue_add_units(q, src, count);
		src += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue consumer to wake up this task, when there is
		 * more room in the queue.
		 */
		task_wait_event(0);
	}
}

static void queue_blocking_remove(struct queue const *q, void *dest,
				  size_t count)
{
	while (true) {
		size_t progress = queue_remove_units(q, dest, count);
		dest += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue consumer to wake up this task, when there is
		 * more data in the queue.
		 */
		task_wait_event(0);
	}
}

/*
 * Declaration of header used in the binary USB protocol (Google HyperDebug
 * extensions to CMSIS-DAP protocol.)
 */
struct gpio_monitoring_header_t {
	/* Size of this struct, including the size field. */
	uint16_t transcript_offset;

	/*
	 * Non-zero status indicates an error processing the request, in such
	 * case the other fields may not be valid.
	 */
	uint16_t status;

	/* Bitfield of the level of each of the signals at the beginning of this
	 * transcript. */
	uint16_t start_levels;

	/* Number of bytes of transcript following this struct. */
	uint16_t transcript_size;

	/* Time window covered by this transcript. */
	uint64_t start_timestamp;
	uint64_t end_timestamp;
};

/* Sub-requests */
const uint8_t GPIO_REQ_MONITORING_READ = 0x00;

/* Values for gpio_monitoring_header_t::status */
const uint16_t MON_SUCCESS = 0;
/* Specified GPIO not recognized by HyperDebug */
const uint16_t MON_UNKNOWN_GPIO = 1;
/* Specified GPIO not being monitored */
const uint16_t MON_GPIO_NOT_MONITORED = 2;
/* Specified list of GPIOs span several monitoring groups */
const uint16_t MON_GPIO_MIXED = 3;
/* Specified list of GPIOs fails to include some pins from the group */
const uint16_t MON_GPIO_MISSING = 4;
/* Buffer overrun, returned data is incomplete */
const uint16_t MON_BUFFER_OVERRUN = 5;

/*
 * Entry point for CMSIS-DAP vendor command for GPIO monitoring.
 */
void dap_goog_gpio_monitoring(size_t peek_c)
{
	/*
	 * We need to inspect sub-command on second byte below, in order to
	 * start decoding.
	 */
	if (peek_c < 2)
		return;

	switch (rx_buffer[1]) {
	case GPIO_REQ_MONITORING_READ: {
		/*
		 * Essentially the same as console command `gpio monitoring
		 * read`, but with binary protocol for greatly improved
		 * efficiency.
		 */
		if (peek_c < 3)
			return;
		int gpio_num = rx_buffer[2];
		int gpios[16];
		struct cyclic_buffer_header_t *buf = NULL;
		int gpio_signals_by_no[16];
		queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 3);
		for (int i = 0; i < gpio_num; i++) {
			uint8_t str_len;
			queue_blocking_remove(&cmsis_dap_rx_queue, &str_len, 1);
			queue_blocking_remove(&cmsis_dap_rx_queue, rx_buffer,
					      str_len);
			rx_buffer[str_len] = '\0';
			gpios[i] = gpio_find_by_name(rx_buffer);
		}

		/*
		 * Start the one-byte CMSIS-DAP encapsulation header at offset 7
		 * in tx_buffer, such that our header struct which follows it
		 * will be 8-byte aligned.
		 */
		struct gpio_monitoring_header_t *header =
			(struct gpio_monitoring_header_t *)(tx_buffer + 8);
		uint8_t *const encapsulated_header = tx_buffer + 7;
		const size_t encapsulated_header_size = 1 + sizeof(*header);
		encapsulated_header[0] = tx_buffer[0];
		memset(header, 0, sizeof(*header));
		header->transcript_offset = sizeof(*header);
		header->status = MON_SUCCESS;

		for (int i = 0; i < gpio_num; i++) {
			if (gpios[i] == GPIO_COUNT)
				header->status = MON_UNKNOWN_GPIO;
			struct monitoring_slot_t *slot =
				monitoring_slots +
				GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
			if (slot->gpio_signal != gpios[i]) {
				header->status = MON_GPIO_NOT_MONITORED;
			}
			if (buf == NULL) {
				buf = slot->buffer;
			} else if (buf != slot->buffer) {
				header->status = MON_GPIO_MIXED;
			}
			gpio_signals_by_no[slot->signal_no] = gpios[i];
		}
		if (gpio_num != buf->num_signals) {
			header->status = MON_GPIO_MISSING;
		}

		if (header->status != 0) {
			/* Report error processing the request. */
			queue_add_units(&cmsis_dap_tx_queue,
					encapsulated_header,
					encapsulated_header_size);
			return;
		}

		uint32_t start_levels = 0;
		for (uint8_t signal_no = 0; signal_no < buf->num_signals;
		     signal_no++) {
			uint32_t mask =
				gpio_list[gpio_signals_by_no[signal_no]].mask;
			struct monitoring_slot_t *slot =
				monitoring_slots + GPIO_MASK_TO_NUM(mask);
			if (slot->tail_level)
				start_levels |= 1 << signal_no;
		}
		header->start_levels = start_levels;
		header->start_timestamp = buf->tail_time.val;
		timestamp_t now = get_time();
		header->end_timestamp = now.val;

		const uint8_t *tail = traverse_buffer(buf, gpio_signals_by_no,
						      now, (size_t)-1, NULL);

		if (buf->overrun) {
			/*
			 * Report overrun, but still transmit the events that we
			 * managed to capture.
			 */
			header->status = MON_BUFFER_OVERRUN;
		}

		/*
		 * Having found the byte range that corresponds to the time
		 * interval in the header, and having updated `tail_level` and
		 * `tail_time` to match the end of the interval, we can now
		 * transmit all the raw bytes of the range.  If it wraps around
		 * the cyclic buffer, we need two calls to `queue_add_units` (in
		 * addition to first call to transmit the header).
		 */

		if (buf->tail <= tail) {
			/* One contiguous range */
			header->transcript_size = tail - buf->tail;
			queue_add_units(&cmsis_dap_tx_queue,
					encapsulated_header,
					encapsulated_header_size);
			queue_blocking_add(&cmsis_dap_tx_queue, buf->tail,
					   header->transcript_size);
		} else {
			/* Data wraps around */
			header->transcript_size =
				tail - buf->data + buf->end - buf->tail;
			queue_add_units(&cmsis_dap_tx_queue,
					encapsulated_header,
					encapsulated_header_size);
			queue_blocking_add(&cmsis_dap_tx_queue, buf->tail,
					   buf->end - buf->tail);
			queue_blocking_add(&cmsis_dap_tx_queue, buf->data,
					   tail - buf->data);
		}

		buf->tail = tail;
		return;
	}
	}
}
