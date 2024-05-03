/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug GPIO logic and console commands */

#include "atomic.h"
#include "builtin/assert.h"
#include "clock_chip.h"
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

/*
 * Description of the CMSIS-DAP Google vendor extension for GPIO bitbanging.
 * Requests and responses begin with a single byte (0x83).  The standard
 * CMSIS-DAP protocol has all requests fitting in a single 64-byte USB packet.
 * Our extension does not adhere to that convention, and treats the USB
 * interface as a stream of data without paying attention to packet
 * boundaries.
 *
 * Command GPIO bitbanging (Host to Device):
 *
 * This request will start or continue a bitbanging waveform.  The set of pins
 * to operate on, and the clock rate, must have been previously specified
 * using the "gpio bit-bang" console command.
 *
 * The waveform data bytes encode runs of samples to be clocked out, with
 * optional delays in between.  A run of data bytes is encoded with one byte for
 * for each clock tick, all having the MSB (DELAY_BIT) equal to zero, while the
 * seven least significant bits encoding values for each of up to seven pins
 * (starting from the LSB).  A delay between runs is encoded as one or more
 * bytes with their MSB (DELAY_BIT) set to one.  The low seven bits from each
 * such "cluster" is to be all concatenated (least significant bits in first
 * bytes), in order to form an integer number of clock ticks of delay.  A delay
 * of one tick is equivalent to repeating the last sample (and thus does not
 * save any memory), a delay of zero ticks is invalid.
 *
 * +----------------+---------------+-----------------+---------------+
 * | cmsis_cmd : 1B | gpio_cmd : 1B | data count : 2B | data  (>= 0B) |
 * +----------------+---------------+-----------------+---------------+
 *
 * cmsis_cmd:     DAP_GOOG_Gpio              (0x83)
 *
 * gpio_cmd:      one of:
 *                GPIO_REQ_BITBANG           (0x10)
 *                GPIO_REQ_BITBANG_STREAMING (0x11)
 *
 * data count:    2 byte, zero based count of bytes to follow
 *
 * data:          Up to 65535 bytes of data of bitbanging waveform (format
 *                described above).  The caller should not send more data than
 *                what has been indicated to be available by the "free count"
 *                field of a previous response.
 *
 *
 * Response GPIO bitbanging (Device to Host):
 *
 * For each byte of waveform data sent from host to device (as part of above
 * command type), one byte will eventually be returned from device to host using
 * this response type (possibly in the immediate response, possibly in a later
 * one).  The returned bytes will mirror the runs of sample and delay in the
 * request stream.  Each sample byte in the response will contain the values of
 * the involved pins as seen just before the waveform data was applied.  That
 * is, the values will be shifted by one sample.  push-pull pins will always
 * read back the same value as from the previous byte in the waveform data,
 * open-drain may or may not, and input pins will be unaffected by the value in
 * the waveform data.  Delay encodings are passed back unchanged.
 *
 * +---------------+------------+--------------+--------------+--------------+
 * | cmsis_cmd: 1B | status: 1B | free cnt: 2B | data cnt: 2B | data (>= 0B) |
 * +---------------+------------+--------------+--------------+--------------+
 *
 * cmsis_cmd:     DAP_GOOG_Gpio              (0x83)
 *
 * status:        one of:
 *                STATUS_BITBANG_IDLE        (0x00)
 *                STATUS_BITBANG_ONGOING     (0x01)
 *                STATUS_ERROR_WAVEFORM      (0x80)
 *
 * free count:    2 byte, indicates how many bytes of buffer space will be free
 *                after this response has been offloaded by HyperDebug.  That
 *                is, this is the maximum number of bytes that the host can
 *                safely transmit in the next GPIO bitbanging command.  The host
 *                is encouraged to send a zero-byte GPIO bitbanging command the
 *                first time, for the sole purpose of learning the buffer size
 *                of HyperDebug.
 *
 * data count:    2 byte, zero based count of bytes to follow
 *
 * data:          Up to 65535 bytes of data of bitbanging waveform (format
 *                described above).
 *
 */

/* Size of buffer used for bitbanging waveform. */
#define BITBANG_BUFFER_SIZE 16384

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
 * timestamps: tail_time is the time of the most recently inserted event, and is
 * accessed and updated only by the interrupt handler.  head_time is the past
 * timestamp on which the diff of the oldest record in the buffer is based (the
 * timestamp of the last record to be removed from the buffer), it is accessed
 * and updated only from the non-interrupt code that removes records from the
 * buffer.
 *
 * In a similar fashion, the signal level is recorded "at both ends" in for each
 * monitored signal by tail_level and head_level, the former only accessed from
 * the interrupt handler, and the latter only accessed from non-interrupt code.
 */
struct cyclic_buffer_header_t {
	/* Time base that the oldest event is relative to. */
	timestamp_t head_time;
	/* Time of the most recent event, updated from interrupt context. */
	volatile uint32_t tail_time;
	/* Index at which new records are placed, updated from interrupt. */
	uint8_t *volatile tail;
	/* Index of oldest record. */
	const uint8_t *head;
	/*
	 * End of cyclic byte buffer. Here tail and head will wrap back to the
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
	volatile uint32_t tail_level;
	/*
	 * Level as of the current oldest end (head) of the recording. (0: low,
	 * gpio_pin_mask: high).
	 */
	uint32_t head_level;
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

	/* Prepare timer for use in GPIO bit-banging. */
	__hw_timer_enable_clock(BITBANG_TIMER, 1);
	task_enable_irq(IRQ_TIM(BITBANG_TIMER));
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
 * Configure drive speed of a given pin, mostly useful for SPI pins if clock
 * frequency is to exceed 10MHz.  The STM32L5 datasheet defines four levels 0-3,
 * higher numbers mean faster slew rate, default for all pins is level 0.
 */
static int command_gpio_set_speed(int argc, const char **argv)
{
	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	int gpio = gpio_find_by_name(argv[2]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM2;

	char *e;
	int speed = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;
	if (speed < 0 || speed > 3)
		return EC_ERROR_PARAM3;

	int index = GPIO_MASK_TO_NUM(gpio_list[gpio].mask);

	uint32_t register_value = STM32_GPIO_OSPEEDR(gpio_list[gpio].port);
	register_value &= ~(3U << (index * 2));
	register_value |= speed << (index * 2);
	STM32_GPIO_OSPEEDR(gpio_list[gpio].port) = register_value;

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

	buf->head = buf->tail = buf->data;
	buf->end = buf->data + cyclic_buffer_size;
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
		slot->tail_level = slot->head_level =
			gpio_get_level(gpios[i]) ? gpio_list[gpios[i]].mask : 0;
		/*
		 * Race condition here!  If three or more edges happen in
		 * rapid succession, we may fail to record some of them, but
		 * we should never over-report edges.
		 *
		 * Since edge detection was enabled before the "tail_level"
		 * was polled, if an edge happened between the two, then an
		 * interrupt is currently pending, and when handled after this
		 * loop, the logic in the gpio_edge interrupt handler would
		 * wrongly conclude that the signal must have seen two
		 * transitions, in order to end up at the same level as before.
		 * In order to avoid such over-reporting, we clear "pending"
		 * interrupt bit below, but only for the direction that goes
		 * "towards" the level measured above.
		 */
		if (slot->tail_level)
			STM32_EXTI_RPR = BIT(gpio_num);
		else
			STM32_EXTI_FPR = BIT(gpio_num);
	}
	/*
	 * Now enable the handling of the set of interrupts.
	 */
	now = get_time();
	buf->tail_time = now.le.lo;
	CPU_NVIC_EN(0) = nvic_mask;

	buf->head_time = now;
	num_cur_monitoring++;
	ccprintf("  @%lld\n", buf->head_time.val);

	/*
	 * Dump the initial level of each input, for the convenience of the
	 * caller.  (Allow makes monitoring useful, even if a signal has no
	 * transitions during the monitoring period.
	 */
	for (i = 0; i < gpio_num; i++) {
		slot = monitoring_slots +
		       GPIO_MASK_TO_NUM(gpio_list[gpios[i]].mask);
		ccprintf("  %d %s %d\n", i, gpio_list[gpios[i]].name,
			 !!slot->head_level);
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
	buf->head = traverse_buffer(buf, gpio_signals_by_no, traverse_until, 32,
				    &print_event);
	if (buf->head != buf->tail)
		ccprintf("Warning: more data\n");
	if (buf->overrun)
		ccprintf("Error: Buffer overrun\n");
	return EC_SUCCESS;
}

/*
 * This routine iterates through buffered entries starting from buf->head,
 * stopping when there are no more entries before the `now` timestamp, or when
 * having processed a certain number of entries given by `limit`, whichever
 * comes first.  The return value indicate a new value which the caller must put
 * into buf->head.  As soon as the caller does this, the traversed range is free
 * to be overwritten by the interrupt handler.
 */
static const uint8_t *
traverse_buffer(struct cyclic_buffer_header_t *buf, int gpio_signals_by_no[],
		timestamp_t now, size_t limit,
		void (*process_event)(uint8_t, timestamp_t, bool))
{
	/*
	 * We have read the current time, before taking a snapshot of the tail
	 * pointer as set by the interrupt handler.  This way, we can guarantee
	 * that the transcript will include any edge happening at or before the
	 * `now` timestamp.  If an interrupt happens after `now` was captured,
	 * but before the line below, causing our tail pointer to include an
	 * event that happened after "now", then it and any further entries will
	 * be excluded from the traversal, and remain in the cyclic buffer for
	 * the next invocation of `gpio monitoring read`.
	 */
	const uint8_t *tail = buf->tail;

	int8_t signal_bits = buf->signal_bits;
	const uint8_t *head = buf->head;
	timestamp_t head_time = buf->head_time;
	while (head != tail && limit-- > 0) {
		const uint8_t *const buf_start = buf->data;
		timestamp_t diff;
		uint8_t byte;
		uint8_t signal_no;
		uint32_t mask;
		int shift = 0;
		const uint8_t *tentative_head = head;
		struct monitoring_slot_t *slot;
		diff.val = 0;
		do {
			byte = *tentative_head++;
			if (tentative_head == buf->end)
				tentative_head = buf_start;
			diff.val |= (byte & 0x7F) << shift;
			shift += 7;
		} while (byte & 0x80);
		signal_no = diff.val & (0xFF >> (8 - signal_bits));
		diff.val >>= signal_bits;
		if (head_time.val + diff.val > now.val) {
			/*
			 * Do not consume this or subsequent records, which
			 * apparently happened after our "now" timestamp from
			 * earlier in the execution of this method.
			 */
			break;
		}
		head = tentative_head;
		head_time.val += diff.val;
		mask = gpio_list[gpio_signals_by_no[signal_no]].mask;
		slot = monitoring_slots + GPIO_MASK_TO_NUM(mask);
		slot->head_level ^= mask;
		if (process_event)
			process_event(signal_no, head_time, slot->head_level);
	}
	buf->head_time = head_time;
	return head;
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

/*
 * For speed of interrupt handler, each pin to be manipulated by bitbanging is
 * recorded as the base address of the GPIO bank, as well as the 16-bit "mask"
 * to use to access the particular pin in the bank.
 */
static uint8_t num_bitbang_pins;
static size_t bitbang_pin_bases[7];
static uint32_t bitbang_pin_masks[7];

/*
 * Cyclic buffer storing the waveform to output, as well as recorded samples.
 */
static uint8_t bitbang_data[BITBANG_BUFFER_SIZE];

/*
 * Obtain address into bitbang_data, corresponding to given index.
 */
static inline uint8_t *bitbang_data_ptr(uint32_t idx)
{
	BUILD_ASSERT(POWER_OF_TWO(BITBANG_BUFFER_SIZE));
	return bitbang_data + (idx & (BITBANG_BUFFER_SIZE - 1));
}

/*
 * Organization of bitbang_data.  The indices move to the right, as data is
 * being read/written:
 *
 *      CMSIS reads data           IRQ reads and overwrites     CMSIS writes
 *      v                          v                            v
 * +----+--------------------------+----------------------------+------------+
 * |    | samples to be sent to PC | waveform data from PC      |            |
 * +----+--------------------------+----------------------------+------------+
 *      ^                          ^                         ^  ^
 *      bitbang_head               bitbang_irq               |  bitbang_tail
 *                                                           bitbang_irq_tail
 */

/* Index incremented by CMSIS_DAP task when data arrives from PC. */
static volatile uint32_t bitbang_tail = 0;

/*
 * Index indicating how far the interrupt handler can process, set by CMSIS_DAP
 * task when data arrives from PC.  Usually it will be identical to
 * bitbang_tail, but may lag by a few bytes, in cases when a multi-byte delay
 * encoding has been only partially received from the USB host.
 */
static volatile uint32_t bitbang_irq_tail = 0;

/*
 * Index incremented by timer interrupt handler.  At each tick, the tnterrupt
 * handler will read the byte at this index, and use it do drive GPIO outputs,
 * and then replace it with GPIO input levels as they were measured just before
 * the output levels were applied.
 */
static volatile uint32_t bitbang_irq = 0;

/* Index incremented by CMSIS_DAP task when data is sent to PC. */
static volatile uint32_t bitbang_head = 0;

/*
 * For the cases where encoded data indicates a "pause" of several clock ticks
 * between waveform edges, this counter is used to record how many future
 * interrupts should "do nothing", before the next byte is applied to GPIOs.
 */
static uint32_t bitbang_countdown;

#define BITBANG_DELAY_BIT 0x80
#define BITBANG_DATA_MASK 0x7F

/*
 * Bitbang timer interrupt handler.  Will read the status of GPIOs, then set
 * GPIO output according to the byte at `bitbang_irq`, before overwriting it
 * with the sampled GPIOs and incrementing `bitbang_irq`.  (Except when high
 * bit of byte it set, which means to pause for a number of cycles.)
 */
void IRQ_HANDLER(IRQ_TIM(BITBANG_TIMER))(void)
{
	uint32_t triggered = STM32_TIM_SR(BITBANG_TIMER);
	if (!(triggered & 1)) {
		return;
	}
	STM32_TIM_SR(BITBANG_TIMER) = 0xFFFE;
	if (bitbang_irq == bitbang_irq_tail) {
		/* End of waveform, stop timer */
		STM32_TIM_CR1(BITBANG_TIMER) = 0;
		return;
	}
	if (bitbang_countdown) {
		bitbang_countdown--;
		return;
	}
	uint8_t data_byte = *bitbang_data_ptr(bitbang_irq);
	if (data_byte & BITBANG_DELAY_BIT) {
		/* Maintain current levels for a number of cycles. */
		uint8_t delay_scale = 0;
		bitbang_countdown = 0;
		do {
			bitbang_irq++;
			bitbang_countdown += ((data_byte & BITBANG_DATA_MASK)
					      << delay_scale);
			delay_scale += 7;
			data_byte = *bitbang_data_ptr(bitbang_irq);
		} while (data_byte & BITBANG_DELAY_BIT);
		/* One cycle of delay already spent processing */
		bitbang_countdown--;
		return;
	}
	/*
	 * Read current level of all pins part of bit-banging.  If some of the
	 * pins are in push-pull mode, this will be what was written the
	 * previous tick.
	 */
	uint8_t input_data = 0;
	for (uint8_t i = 0; i < num_bitbang_pins; i++) {
		input_data |= !!(STM32_GPIO_IDR(bitbang_pin_bases[i]) &
				 bitbang_pin_masks[i])
			      << i;
	}
	*bitbang_data_ptr(bitbang_irq++) = input_data;
	/*
	 * Set drive of all pins which are part of bit-banging.  If some of the
	 * pins are in input mode, this will have no effect.
	 */
	for (uint8_t i = 0; i < num_bitbang_pins; i++) {
		if (data_byte & (1 << i)) {
			STM32_GPIO_BSRR(bitbang_pin_bases[i]) =
				bitbang_pin_masks[i];
		} else {
			STM32_GPIO_BSRR(bitbang_pin_bases[i]) =
				bitbang_pin_masks[i] << 16;
		}
	}
}

/*
 * Bitbanging timer interrupt one level below the GPIO edge detection
 * interrupts.  If more than one of the pins being bitbanged are also being
 * monitored, this allows accurately recording which pin is modified first at a
 * particular clock tick, as the edge interrupt would run for each iteration of
 * the loop in the bitbanging interrupt handler above.  Leaving them at same
 * priority would mean that all edge detection interrupts would run after the
 * bitbanging handler, probably in order of the pin number, which could lead to
 * falsely reversing the order of e.g. edges on SDA and SCL, which would impact
 * the meaning of I2C signals.
 */
const struct irq_priority __keep IRQ_PRIORITY(IRQ_TIM(BITBANG_TIMER))
	__attribute__((weak, section(".rodata.irqprio"))) = {
		IRQ_TIM(BITBANG_TIMER), 1
	};

static void stop_all_gpio_bitbanging(void)
{
	/* Stop timer */
	STM32_TIM_CR1(BITBANG_TIMER) = 0;

	/*
	 * Empty the queue.
	 *
	 * CAUTION: No guard against CMSIS-DAP task simultaneously operating on
	 * the queue, we count on OpenTitanTool not simultaneously requesting
	 * big-banging via one USB endpoint and re-initialization on another.
	 */
	bitbang_tail = 0;
	bitbang_irq = 0;
	bitbang_irq_tail = 0;
	bitbang_head = 0;
}

static int command_gpio_bit_bang(int argc, const char **argv)
{
	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;
	int gpio_num = argc - 3;
	if (gpio_num > 7)
		return EC_ERROR_PARAM_COUNT;

	const uint32_t timer_freq = clock_get_timer_freq();
	char *e;
	uint64_t desired_period_ns = strtoull(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (desired_period_ns > 0xFFFFFFFFFFFFFFFFULL / timer_freq) {
		/* Would overflow below. */
		return EC_ERROR_PARAM3;
	}

	/*
	 * Calculate number of hardware timer cycles for each bit-banging
	 * sample.
	 */
	uint64_t divisor = desired_period_ns * timer_freq / 1000000000;

	if (divisor > (1ULL << 32)) {
		/* Would overflow the 32-bit timer. */
		return EC_ERROR_PARAM3;
	}

	int gpios[7];
	for (int i = 0; i < gpio_num; i++) {
		gpios[i] = gpio_find_by_name(argv[3 + i]);
		if (gpios[i] == GPIO_COUNT) {
			return EC_ERROR_PARAM3 + i;
		}
	}

	if (STM32_TIM_CR1(BITBANG_TIMER) & STM32_TIM_CR1_CEN) {
		ccprintf("Error: Ongoing operation, cannot change settings.\n");
		return EC_ERROR_INVAL;
	}

	/*
	 * All input valid, now record the request.
	 */
	num_bitbang_pins = gpio_num;
	for (int i = 0; i < num_bitbang_pins; i++) {
		bitbang_pin_bases[i] = gpio_list[gpios[i]].port;
		bitbang_pin_masks[i] = gpio_list[gpios[i]].mask;
	}

	/* Set clock divisor to achieve requested tick period. */
	STM32_TIM32_ARR(BITBANG_TIMER) = divisor - 1;

	/* Update prescaler to increment every tick */
	STM32_TIM_PSC(BITBANG_TIMER) = 0;

	/* Set up the overflow interrupt */
	STM32_TIM_SR(BITBANG_TIMER) = 0;
	STM32_TIM_DIER(BITBANG_TIMER) = 0x0001;

	return EC_SUCCESS;
}

static int command_gpio(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "analog-set"))
		return command_gpio_analog_set(argc, argv);
	if (!strcasecmp(argv[1], "set-speed"))
		return command_gpio_set_speed(argc, argv);
	if (!strcasecmp(argv[1], "monitoring"))
		return command_gpio_monitoring(argc, argv);
	if (!strcasecmp(argv[1], "multiset"))
		return command_gpio_multiset(argc, argv);
	if (!strcasecmp(argv[1], "set-reset"))
		return command_gpio_set_reset(argc, argv);
	if (!strcasecmp(argv[1], "bit-bang"))
		return command_gpio_bit_bang(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(
	gpio, command_gpio,
	"multiset name [level] [mode] [pullmode] [milli_volts]"
	"\nanalog-set name milli_volts"
	"\nset-speed name 0-3"
	"\nset-reset name"
	"\nmonitoring start name..."
	"\nmonitoring read name..."
	"\nmonitoring stop name..."
	"\nbit-bang clock_ns name...",
	"GPIO manipulation", CMD_FLAG_RESTRICTED);

static void gpio_reinit(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	stop_all_gpio_monitoring();
	stop_all_gpio_bitbanging();

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
const uint8_t GPIO_REQ_BITBANG = 0x10;
const uint8_t GPIO_REQ_BITBANG_STREAMING = 0x11;

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

static void dap_goog_gpio_monitoring_read(size_t peek_c)
{
	/*
	 * Essentially the same as console command `gpio monitoring read`, but
	 * with binary protocol for greatly improved efficiency.
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
		queue_blocking_remove(&cmsis_dap_rx_queue, rx_buffer, str_len);
		rx_buffer[str_len] = '\0';
		gpios[i] = gpio_find_by_name(rx_buffer);
	}
	if (cmsis_dap_unwind_requested())
		return;

	/*
	 * Start the one-byte CMSIS-DAP encapsulation header at offset 7 in
	 * tx_buffer, such that our header struct which follows it will be
	 * 8-byte aligned.
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
		queue_add_units(&cmsis_dap_tx_queue, encapsulated_header,
				encapsulated_header_size);
		return;
	}

	uint32_t start_levels = 0;
	for (uint8_t signal_no = 0; signal_no < buf->num_signals; signal_no++) {
		uint32_t mask = gpio_list[gpio_signals_by_no[signal_no]].mask;
		struct monitoring_slot_t *slot =
			monitoring_slots + GPIO_MASK_TO_NUM(mask);
		if (slot->head_level)
			start_levels |= 1 << signal_no;
	}
	header->start_levels = start_levels;
	header->start_timestamp = buf->head_time.val;
	timestamp_t now = get_time();
	header->end_timestamp = now.val;

	const uint8_t *head =
		traverse_buffer(buf, gpio_signals_by_no, now, (size_t)-1, NULL);

	if (buf->overrun) {
		/*
		 * Report overrun, but still transmit the events that we managed
		 * to capture.
		 */
		header->status = MON_BUFFER_OVERRUN;
	}

	/*
	 * Having found the byte range that corresponds to the time interval in
	 * the header, and having updated `head_level` and `head_time` to match
	 * the end of the interval, we can now transmit all the raw bytes of the
	 * range.  If it wraps around the cyclic buffer, we need two calls to
	 * `queue_add_units` (in addition to first call to transmit the header).
	 */

	if (buf->head <= head) {
		/* One contiguous range */
		header->transcript_size = head - buf->head;
		queue_add_units(&cmsis_dap_tx_queue, encapsulated_header,
				encapsulated_header_size);
		queue_blocking_add(&cmsis_dap_tx_queue, buf->head,
				   header->transcript_size);
	} else {
		/* Data wraps around */
		header->transcript_size =
			head - buf->data + buf->end - buf->head;
		queue_add_units(&cmsis_dap_tx_queue, encapsulated_header,
				encapsulated_header_size);
		queue_blocking_add(&cmsis_dap_tx_queue, buf->head,
				   buf->end - buf->head);
		queue_blocking_add(&cmsis_dap_tx_queue, buf->data,
				   head - buf->data);
	}

	buf->head = head;
}

const uint8_t STATUS_BITBANG_IDLE = 0x00;
const uint8_t STATUS_BITBANG_ONGOING = 0x01;
const uint8_t STATUS_ERROR_WAVEFORM = 0x80;

/*
 * Validate data from previous bitbang_irq_tail to bitbang_tail + data_len.
 * That is possibly a few bytes from the tail end of the most recent data, plus
 * anything received in this request.  Return non-zero on invalid data, if data
 * is valid, increments bitbang_tail by exactly data_len, and sets
 * bitbang_irq_tail at or a few bytes before the new bitbang_tail.
 */
static uint8_t validate_received_waveform(uint16_t data_len, bool streaming)
{
	uint32_t tail_goal = bitbang_tail + data_len;

	uint32_t idx = bitbang_irq_tail;
	uint32_t valid_idx = idx;
	while (idx != tail_goal) {
		if (*bitbang_data_ptr(idx) & BITBANG_DELAY_BIT) {
			uint8_t delay_scale = 0;
			while (idx != tail_goal &&
			       *bitbang_data_ptr(idx) & BITBANG_DELAY_BIT) {
				/*
				 * Shifting right by 32 - delay_scale
				 * effectively gives us the low bytes of the
				 * upper 32 bits of what we would have gotten by
				 * shifting left by delay_scale.  If that is
				 * non-zero, it means that the encoded value
				 * would exceed 32 bits.
				 */
				if ((*bitbang_data_ptr(idx) &
				     BITBANG_DATA_MASK) >>
				    (32 - delay_scale)) {
					return STATUS_ERROR_WAVEFORM;
				}
				delay_scale += 7;
				idx++;
			}
		} else {
			idx++;
			valid_idx = idx;
		}
	}

	if (!streaming && valid_idx != bitbang_tail + data_len) {
		/*
		 * Possibly incomplete delay encoding at end of waveform, but no
		 * further waveform data is expected.  IRQ handler is not coded
		 * to be able to handle delay not followed by at least one
		 * waveform sample, so we have to reject this.
		 */
		return STATUS_ERROR_WAVEFORM;
	}

	bitbang_tail = tail_goal;
	bitbang_irq_tail = valid_idx;

	return 0;
}

/*
 * Receive more bitbanging data to be inserted at bitbang_tail, then offload
 * data between bitbang_head and bitbang_irq.
 */
void dap_goog_gpio_bitbang(size_t peek_c, bool streaming)
{
	if (peek_c < 4)
		return;

	uint16_t data_len = rx_buffer[2] + (rx_buffer[3] << 8);
	queue_advance_head(&cmsis_dap_rx_queue, 4);

	uint8_t *tail_ptr = bitbang_data_ptr(bitbang_tail);
	if (tail_ptr + data_len <= bitbang_data + sizeof(bitbang_data)) {
		queue_blocking_remove(&cmsis_dap_rx_queue, tail_ptr, data_len);
	} else {
		uint16_t remaning_space =
			bitbang_data + sizeof(bitbang_data) - tail_ptr;
		queue_blocking_remove(&cmsis_dap_rx_queue, tail_ptr,
				      remaning_space);
		queue_blocking_remove(&cmsis_dap_rx_queue, bitbang_data,
				      data_len - remaning_space);
	}
	if (cmsis_dap_unwind_requested())
		return;

	uint8_t status = validate_received_waveform(data_len, streaming);
	if (status != 0) {
		stop_all_gpio_bitbanging();

		/* How much buffer space is free. */
		uint16_t free_bytes =
			bitbang_irq + BITBANG_BUFFER_SIZE - bitbang_tail;

		tx_buffer[1] = status;
		*(uint16_t *)(tx_buffer + 2) = free_bytes;
		*(uint16_t *)(tx_buffer + 4) = 0;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 6);
		return;
	}

	uint32_t timer_cr1 = STM32_TIM_CR1(BITBANG_TIMER);
	if (!(timer_cr1 & STM32_TIM_CR1_CEN) &&
	    bitbang_irq_tail != bitbang_irq) {
		/*
		 * Hardware timer is not running, and we have received one or
		 * more byte of bitbang waveform.  This means that it is time
		 * to start the timer, so that the next interrupt will begin
		 * producing the waveform.
		 */
		uint32_t divisor = STM32_TIM32_ARR(BITBANG_TIMER);

		/* Number of timer increments per millisecond. */
		uint32_t counts_in_1ms = clock_get_timer_freq() / 1000;

		if (divisor > counts_in_1ms) {
			/*
			 * Slow bit-banging clock.  Use non-zero counter start
			 * value, such that the first overflow interrupt will
			 * happen in one millisecond, rather than waiting for a
			 * full clock tick delay, which could be multiple
			 * seconds.
			 */
			STM32_TIM32_CNT(BITBANG_TIMER) =
				divisor - counts_in_1ms;
			bitbang_countdown = 0;
		} else {
			/*
			 * Fast bit-banging clock.  First few interrupts may
			 * have higher latency.  In order to avoid jitter in the
			 * bit-banged waveform, set up such that the first three
			 * timer interrupts will be skipped, before the
			 * requested waveform begins.
			 */
			STM32_TIM32_CNT(BITBANG_TIMER) = 0;
			bitbang_countdown = 3;
		}

		/* Start counting */
		STM32_TIM_CR1(BITBANG_TIMER) |= STM32_TIM_CR1_CEN;
	}

	/*
	 * At this point, the timer interrupt is clocking out data, and
	 * placing sampled values into the same buffer.  For streaming
	 * requests, we want to send a reply once half of the given data has
	 * been processed, for non-streaming, we want to wait until all the
	 * data has been processed.
	 *
	 * In any case, we do not want to delay responding to the USB request
	 * for too long, as that could cause timeout in the handling on the host
	 * computer.  So if necessary, we will respond with fewer bytes of data
	 * than indicated above, possibly no data bytes at all, in which case
	 * the host computer will have to issue a new USB request (probably with
	 * zero bytes of waveform data), in order to inquire if data has become
	 * available.
	 */
	timestamp_t start = get_time();
	const uint32_t MAX_USB_RESPONSE_TIME_US = 25000;
	do {
		if (!streaming) {
			if (!(STM32_TIM_CR1(BITBANG_TIMER) & STM32_TIM_CR1_CEN))
				break;
		} else {
			uint16_t used_bytes = bitbang_tail - bitbang_head;
			if (bitbang_irq - bitbang_head >= used_bytes / 2)
				break;
		}
	} while (time_since32(start) < MAX_USB_RESPONSE_TIME_US);

	uint32_t idx = bitbang_irq;
	tx_buffer[1] = bitbang_head != bitbang_tail ? STATUS_BITBANG_ONGOING :
						      STATUS_BITBANG_IDLE;

	/* Number of data bytes to return in this response. */
	data_len = idx - bitbang_head;

	/* How much buffer space will be free after sending this response. */
	uint16_t free_bytes = idx + BITBANG_BUFFER_SIZE - bitbang_tail;

	*(uint16_t *)(tx_buffer + 2) = free_bytes;
	*(uint16_t *)(tx_buffer + 4) = data_len;

	uint8_t *head_ptr = bitbang_data_ptr(bitbang_head);
	if (head_ptr + data_len <= bitbang_data + sizeof(bitbang_data)) {
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 6);
		queue_blocking_add(&cmsis_dap_tx_queue, head_ptr, data_len);
	} else {
		uint16_t remaining_space =
			bitbang_data + sizeof(bitbang_data) - head_ptr;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 6);
		queue_blocking_add(&cmsis_dap_tx_queue, head_ptr,
				   remaining_space);
		queue_blocking_add(&cmsis_dap_tx_queue, bitbang_data,
				   data_len - remaining_space);
	}
	bitbang_head = idx;
}

/*
 * Entry point for CMSIS-DAP vendor command for GPIO operations.
 *
 * CAUTION: This handler routine runs on the CMSIS-DAP task, and the code below
 * may block waiting to receive/send data via USB.  This has the potential to
 * conflict with the console task, particularly if that one invokes a function
 * like `stop_all_gpio_bitbanging()`, which modifies the same state as methods
 * below.
 *
 * As long as clients behave, and do not simultaneously request monitoring or
 * bitbanging operations though the CMSIS-DAP interface while also sending
 * `reinit` console command, the one case we are worried about is a bitbanging
 * or monitoring client having stopped "in the middle" of performing some
 * CMSIS-DAP operation, leaving the CMSIS-DAP task stuck in one of the handler
 * functions in this file.  Then the next test session would presumably start by
 * invoking `reinit`, which will be handled this way: In `cmsis-dap.c` a REINIT
 * hook is registered with high priority, which will set
 * `cmsis_dap_unwind_requested()` and will cause any blocking queue operation of
 * the CMSIS-DAP task to exit.  Handler functions above will respond by exiting
 * immediately, even if that means possibly leaving inconsistent state (such as
 * having updated `head_level` but not moved the `head` pointer to match).  The
 * normal priority REINIT hook in this file will then be called, which resets
 * the state, such that it will be in a consistent and known initial state.
 */
void dap_goog_gpio(size_t peek_c)
{
	/*
	 * We need to inspect sub-command on second byte below, in order to
	 * start decoding.
	 */
	if (peek_c < 2)
		return;

	switch (rx_buffer[1]) {
	case GPIO_REQ_MONITORING_READ:
		/*
		 * Hand off all available GPIO monitoring data so far,
		 * suitable for streaming.
		 */
		dap_goog_gpio_monitoring_read(peek_c);
		break;
	case GPIO_REQ_BITBANG:
		/*
		 * Accept data for bitbanging, wait for waveform to be
		 * complete, and then hand back data polled during.
		 */
		dap_goog_gpio_bitbang(peek_c, false);
		break;
	case GPIO_REQ_BITBANG_STREAMING:
		/*
		 * Accept data for bitbanging, hand back available data, while
		 * waveform still in process, suitable for streaming if
		 * invoked again before data runs out.
		 */
		dap_goog_gpio_bitbang(peek_c, true);
		break;
	}
}
