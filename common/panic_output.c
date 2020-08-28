/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "printf.h"
#include "software_panic.h"
#include "sysjump.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_console.h"
#include "util.h"

/* Panic data goes at the end of RAM. */
static struct panic_data * const pdata_ptr = PANIC_DATA_PTR;

/* Common SW Panic reasons strings */
const char * const panic_sw_reasons[] = {
#ifdef CONFIG_SOFTWARE_PANIC
	"PANIC_SW_DIV_ZERO",
	"PANIC_SW_STACK_OVERFLOW",
	"PANIC_SW_PD_CRASH",
	"PANIC_SW_ASSERT",
	"PANIC_SW_WATCHDOG",
	"PANIC_SW_RNG",
	"PANIC_SW_PMIC_FAULT",
#endif
};

/**
 * Check an interrupt vector as being a valid software panic
 * @param reason	Reason for panic
 * @return 0 if not a valid software panic reason, otherwise non-zero.
 */
int panic_sw_reason_is_valid(uint32_t reason)
{
	return (IS_ENABLED(CONFIG_SOFTWARE_PANIC) &&
		reason >= PANIC_SW_BASE &&
		(reason - PANIC_SW_BASE) < ARRAY_SIZE(panic_sw_reasons));
}

/**
 * Add a character directly to the UART buffer.
 *
 * @param context	Context; ignored.
 * @param c		Character to write.
 * @return 0 if the character was transmitted, 1 if it was dropped.
 */
#ifndef CONFIG_DEBUG_PRINTF
static int panic_txchar(void *context, int c)
{
	if (c == '\n')
		panic_txchar(context, '\r');

	/* Wait for space in transmit FIFO */
	while (!uart_tx_ready())
		;

	/* Write the character directly to the transmit FIFO */
	uart_write_char(c);

	return 0;
}

void panic_puts(const char *outstr)
{
	/* Flush the output buffer */
	uart_flush_output();

	/* Put all characters in the output buffer */
	while (*outstr)
		panic_txchar(NULL, *outstr++);

	/* Flush the transmit FIFO */
	uart_tx_flush();
}

void panic_printf(const char *format, ...)
{
	va_list args;

	/* Flush the output buffer */
	uart_flush_output();

	va_start(args, format);
	/* Send the message to the UART console */
	vfnprintf(panic_txchar, NULL, format, args);
#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)
	/* Send the message to the USB console on platforms which support it. */
	usb_vprintf(format, args);
#endif

	va_end(args);

	/* Flush the transmit FIFO */
	uart_tx_flush();
}
#endif

/**
 * Display a message and reboot
 */
void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}

#ifdef CONFIG_DEBUG_ASSERT_REBOOTS
#ifdef CONFIG_DEBUG_ASSERT_BRIEF
void panic_assert_fail(const char *fname, int linenum)
{
	panic_printf("\nASSERTION FAILURE at %s:%d\n", fname, linenum);
#ifdef CONFIG_SOFTWARE_PANIC
	software_panic(PANIC_SW_ASSERT, linenum);
#else
	panic_reboot();
#endif
}
#else
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	panic_printf("\nASSERTION FAILURE '%s' in %s() at %s:%d\n",
		     msg, func, fname, linenum);
#ifdef CONFIG_SOFTWARE_PANIC
	software_panic(PANIC_SW_ASSERT, linenum);
#else
	panic_reboot();
#endif
}
#endif
#endif

void panic(const char *msg)
{
	panic_printf("\n** PANIC: %s\n", msg);
	panic_reboot();
}

struct panic_data *panic_get_data(void)
{
	BUILD_ASSERT(sizeof(struct panic_data) <= CONFIG_PANIC_DATA_SIZE);

	if (pdata_ptr->magic != PANIC_DATA_MAGIC ||
	    pdata_ptr->struct_size != CONFIG_PANIC_DATA_SIZE)
		return NULL;

	return pdata_ptr;
}

/*
 * Returns pointer to beginning of panic data.
 * Please note that it is not safe to interpret this
 * pointer as panic_data structure.
 */
uintptr_t get_panic_data_start(void)
{
	if (pdata_ptr->magic != PANIC_DATA_MAGIC)
		return 0;

	return ((uintptr_t)CONFIG_PANIC_DATA_BASE
			   + CONFIG_PANIC_DATA_SIZE
			   - pdata_ptr->struct_size);
}

static uint32_t get_panic_data_size(void)
{
	if (pdata_ptr->magic != PANIC_DATA_MAGIC)
		return 0;

	return pdata_ptr->struct_size;
}

/*
 * Returns pointer to panic_data structure that can be safely written.
 * Please note that this function can move jump data and jump tags.
 * It can also delete panic data from previous boot, so this function
 * should be used when we are sure that we don't need it.
 */
struct panic_data *get_panic_data_write(void)
{
	/*
	 * Pointer to panic_data structure. It may not point to
	 * the beginning of structure, but accessing struct_size
	 * and magic is safe because it is always placed at the
	 * end of RAM.
	 */
	struct panic_data * const pdata_ptr = PANIC_DATA_PTR;
	const struct jump_data *jdata_ptr;
	uintptr_t data_begin;
	size_t move_size;
	int delta;

	/*
	 * If panic data exists, jump data and jump tags should be moved
	 * about difference between size of panic_data structure and size of
	 * structure that is present in memory.
	 *
	 * If panic data doesn't exist, lets create place for a one
	 */
	if (pdata_ptr->magic == PANIC_DATA_MAGIC)
		delta = CONFIG_PANIC_DATA_SIZE - pdata_ptr->struct_size;
	else
		delta = CONFIG_PANIC_DATA_SIZE;

	/* If delta is 0, there is no need to move anything */
	if (delta == 0)
		return pdata_ptr;

	/*
	 * Expecting get_panic_data_start() will return a pointer to
	 * the beginning of panic data, or NULL if no panic data available
	 */
	data_begin = get_panic_data_start();
	if (!data_begin)
		data_begin = CONFIG_RAM_BASE + CONFIG_RAM_SIZE;

	jdata_ptr = (struct jump_data *)(data_begin - sizeof(struct jump_data));

	/*
	 * If we don't have valid jump_data structure we don't need to move
	 * anything and can just return pdata_ptr (clear memory, set magic
	 * and struct_size first).
	 */
	if (jdata_ptr->magic != JUMP_DATA_MAGIC ||
	    jdata_ptr->version < 1 || jdata_ptr->version > 3) {
		memset(pdata_ptr, 0, CONFIG_PANIC_DATA_SIZE);
		pdata_ptr->magic = PANIC_DATA_MAGIC;
		pdata_ptr->struct_size = CONFIG_PANIC_DATA_SIZE;

		return pdata_ptr;
	}

	if (jdata_ptr->version == 1)
		move_size = JUMP_DATA_SIZE_V1;
	else if (jdata_ptr->version == 2)
		move_size = JUMP_DATA_SIZE_V2 + jdata_ptr->jump_tag_total;
	else if (jdata_ptr->version == 3)
		move_size = jdata_ptr->struct_size + jdata_ptr->jump_tag_total;
	else {
		/* Unknown jump data version - set move size to 0 */
		move_size = 0;
	}

	data_begin -= move_size;

	if (move_size != 0) {
		/* Move jump_tags and jump_data */
		memmove((void *)(data_begin - delta), (void *)data_begin, move_size);
	}

	/*
	 * Now we are sure that there is enough space for current
	 * panic_data structure.
	 */
	memset(pdata_ptr, 0, CONFIG_PANIC_DATA_SIZE);
	pdata_ptr->magic = PANIC_DATA_MAGIC;
	pdata_ptr->struct_size = CONFIG_PANIC_DATA_SIZE;

	return pdata_ptr;
}

static void panic_init(void)
{
#ifdef CONFIG_HOSTCMD_EVENTS
	struct panic_data *addr = panic_get_data();

	/* Notify host of new panic event */
	if (addr && !(addr->flags & PANIC_DATA_FLAG_OLD_HOSTEVENT)) {
		host_set_single_event(EC_HOST_EVENT_PANIC);
		addr->flags |= PANIC_DATA_FLAG_OLD_HOSTEVENT;
	}
#endif
}
DECLARE_HOOK(HOOK_INIT, panic_init, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_CHIPSET_RESET, panic_init, HOOK_PRIO_LAST);

#ifdef CONFIG_CMD_STACKOVERFLOW
static void stack_overflow_recurse(int n)
{
	ccprintf("+%d", n);

	/*
	 * Force task context switch, since that's where we do stack overflow
	 * checking.
	 */
	msleep(10);

	stack_overflow_recurse(n+1);

	/*
	 * Do work after the recursion, or else the compiler uses tail-chaining
	 * and we don't actually consume additional stack.
	 */
	ccprintf("-%d", n);
}
#endif /* CONFIG_CMD_STACKOVERFLOW */

/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_CRASH
static int command_crash(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[1], "assert")) {
		ASSERT(0);
	} else if (!strcasecmp(argv[1], "divzero")) {
		volatile int zero = 0;

		cflush();
		ccprintf("%08x", 1 / zero);
	} else if (!strcasecmp(argv[1], "udivzero")) {
		volatile int zero = 0;

		cflush();
		ccprintf("%08x", 1 / zero);
#ifdef CONFIG_CMD_STACKOVERFLOW
	} else if (!strcasecmp(argv[1], "stack")) {
		stack_overflow_recurse(1);
#endif
	} else if (!strcasecmp(argv[1], "unaligned")) {
		volatile intptr_t unaligned_ptr = 0xcdef;
		cflush();
		ccprintf("%08x", *(volatile int *)unaligned_ptr);
	} else if (!strcasecmp(argv[1], "watchdog")) {
		while (1)
			;
	} else if (!strcasecmp(argv[1], "hang")) {
		interrupt_disable();
		while (1)
			;
	} else {
		return EC_ERROR_PARAM1;
	}

	/* Everything crashes, so shouldn't get back here */
	return EC_ERROR_UNKNOWN;
}
DECLARE_CONSOLE_COMMAND(crash, command_crash,
		"[assert | divzero | udivzero"
#ifdef CONFIG_CMD_STACKOVERFLOW
			" | stack"
#endif
			" | unaligned | watchdog | hang]",
		"Crash the system (for testing)");
#endif

static int command_panicinfo(int argc, char **argv)
{
	struct panic_data * const pdata_ptr = panic_get_data();

	if (pdata_ptr) {
		ccprintf("Saved panic data:%s\n",
			 (pdata_ptr->flags & PANIC_DATA_FLAG_OLD_CONSOLE ?
			  "" : " (NEW)"));

		panic_data_print(pdata_ptr);

		/* Data has now been printed */
		pdata_ptr->flags |= PANIC_DATA_FLAG_OLD_CONSOLE;
	} else {
		ccprintf("No saved panic data available "
		    "or panic data can't be safely interpreted.\n");
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(panicinfo, command_panicinfo,
			NULL,
			"Print info from a previous panic");

/*****************************************************************************/
/* Host commands */

enum ec_status host_command_panic_info(struct host_cmd_handler_args *args)
{
	uint32_t pdata_size = get_panic_data_size();
	uintptr_t pdata_start = get_panic_data_start();
	struct panic_data * pdata;

	if (pdata_start && pdata_size > 0) {
		ASSERT(pdata_size <= args->response_max);
		memcpy(args->response, (void *)pdata_start, pdata_size);
		args->response_size = pdata_size;

		pdata = panic_get_data();
		if (pdata) {
			/* Data has now been returned */
			pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
		}
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PANIC_INFO,
		     host_command_panic_info,
		     EC_VER_MASK(0));
