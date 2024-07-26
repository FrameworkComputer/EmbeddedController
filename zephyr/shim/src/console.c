/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/238433667): Include EC printf functions
 * (crec_vsnprintf/crec_snprintf) until we switch to the standard
 * vsnprintf/snprintf.
 */
#include "builtin/stdio.h"
#include "console.h"
#include "printf.h"
#include "task.h"
#include "uart.h"
#include "usb_console.h"
#include "zephyr_console_shim.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/shell/shell.h>
#ifdef CONFIG_SHELL_BACKEND_DUMMY /* nocheck */
#include <zephyr/shell/shell_dummy.h> /* nocheck */
#endif
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>

#if !defined(CONFIG_SHELL_BACKEND_SERIAL) && \
	!defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
#error Must select either CONFIG_SHELL_BACKEND_SERIAL or \
	CONFIG_SHELL_BACKEND_DUMMY /* nocheck */
#endif
#if defined(CONFIG_SHELL_BACKEND_SERIAL) && \
	defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
#error Must select only one shell backend
#endif

#ifdef CONFIG_PIGWEED_LOG_TOKENIZED_LIB
char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];
#endif

LOG_MODULE_REGISTER(shim_console, LOG_LEVEL_ERR);

__maybe_unused static int (*zephyr_char_out)(int);

static const struct device *uart_shell_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
static const struct shell *shell_zephyr;
static struct k_poll_signal shell_uninit_signal;
static struct k_poll_signal shell_init_signal;
/*
 * A flag is kept to indicate if the shell has been (or is about
 * to be) stopped, so that output won't be sent via zephyr_fprintf()
 * (which requires locking the shell).
 */
static bool shell_stopped;
static bool rx_bypass_enabled;
RING_BUF_DECLARE(rx_buffer, CONFIG_UART_RX_BUF_SIZE);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)

static void uart_rx_handle(const struct device *dev)
{
	static uint8_t scratch;
	static uint8_t *data;
	static uint32_t len, rd_len;

	do {
		/* Get some bytes on the ring buffer */
		len = ring_buf_put_claim(&rx_buffer, &data, rx_buffer.size);
		if (len > 0) {
			/* Read from the FIFO up to `len` bytes */
			rd_len = uart_fifo_read(dev, data, len);

			/* Put `rd_len` bytes on the ring buffer */
			ring_buf_put_finish(&rx_buffer, rd_len);
		} else {
			/*
			 * There's no room on the ring buffer, throw away 1
			 * byte.
			 */
			rd_len = uart_fifo_read(dev, &scratch, 1);
		}
	} while (rd_len != 0 && rd_len == len);
}

test_mockable_static void uart_callback(const struct device *dev,
					void *user_data)
{
	uart_irq_update(dev);

	if (uart_irq_rx_ready(dev))
		uart_rx_handle(dev);
}
#endif

static void shell_uninit_callback(const struct shell *shell, int res)
{
	if (!res) {
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
		/* Set the new callback */
		uart_irq_callback_user_data_set(uart_shell_dev, uart_callback,
						NULL);

		/*
		 * Disable TX interrupts. We don't actually use TX but for some
		 * reason none of this works without this line.
		 */
		uart_irq_tx_disable(uart_shell_dev);

		/* Enable RX interrupts */
		uart_irq_rx_enable(uart_shell_dev);
#endif
	}

	/* Notify the uninit signal that we finished */
	k_poll_signal_raise(&shell_uninit_signal, res);
}

void bypass_cb(const struct shell *shell, uint8_t *data, size_t len)
{
	if (!ring_buf_put(&rx_buffer, data, len)) {
		printk("Failed to write to uart ring buf\n");
	}
}

void uart_shell_rx_bypass(bool enable)
{
	shell_set_bypass(shell_zephyr, enable ? bypass_cb : NULL);
	rx_bypass_enabled = enable;
}

int uart_shell_stop(void)
{
	struct k_poll_event event = K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
		&shell_uninit_signal);

	/*
	 * Set the shell_stopped flag so that no output will
	 * be sent to the uart via zephyr_fprintf after this point.
	 */
	shell_stopped = true;
	/* Clear all pending input */
	uart_clear_input();

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	/* Disable RX and TX interrupts */
	uart_irq_rx_disable(uart_shell_dev);
	uart_irq_tx_disable(uart_shell_dev);
#endif

	/* Initialize the uninit signal */
	k_poll_signal_init(&shell_uninit_signal);

	/* Stop the shell */
	shell_uninit(shell_zephyr, shell_uninit_callback);

	/* Wait for the shell to be turned off, the signal will wake us */
	k_poll(&event, 1, K_FOREVER);

	/* Event was signaled, return the result */
	return event.signal->result;
}

static const struct shell_backend_config_flags shell_cfg_flags =
	SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

static void shell_init_from_work(struct k_work *work)
{
	bool log_backend = 1;
	uint32_t level = CONFIG_LOG_MAX_LEVEL;
	ARG_UNUSED(work);

#ifdef CONFIG_SHELL_BACKEND_SERIAL
	log_backend = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > 0;
	if (CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL <= LOG_LEVEL_DBG)
		level = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL;
#endif

	/* Initialize the shell and re-enable both RX and TX */
	shell_init(shell_zephyr, uart_shell_dev, shell_cfg_flags, log_backend,
		   level);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	uart_irq_rx_enable(uart_shell_dev);
	uart_irq_tx_enable(uart_shell_dev);
#endif

	/* Notify the init signal that initialization is complete */
	k_poll_signal_raise(&shell_init_signal, 0);
}

void uart_shell_start(void)
{
	static struct k_work shell_init_work;
	struct k_poll_event event = K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
		&shell_init_signal);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	/* Disable RX and TX interrupts */
	uart_irq_rx_disable(uart_shell_dev);
	uart_irq_tx_disable(uart_shell_dev);
#endif

	/* Initialize k_work to call shell init (this makes it thread safe) */
	k_work_init(&shell_init_work, shell_init_from_work);

	/* Initialize the init signal to make sure we're read to listen */
	k_poll_signal_init(&shell_init_signal);

	/* Submit the work to be run by the kernel */
	k_work_submit(&shell_init_work);

	/* Wait for initialization to be run, the signal will wake us */
	k_poll(&event, 1, K_FOREVER);
	shell_stopped = false;
}

#ifdef CONFIG_SHELL_HELP
static void print_console_help(const char *name,
			       const struct zephyr_console_command *command)
{
	if (command->help)
		printk("%s\n", command->help);
	if (command->argdesc)
		printk("Usage: %s %s\n", name, command->argdesc);
}
#endif

int zshim_run_ec_console_command(const struct zephyr_console_command *command,
				 size_t argc, const char **argv)
{
	int ret;

	/*
	 * The Zephyr shell only displays the help string and not
	 * the argument descriptor when passing "-h" or "--help".  Mimic the
	 * cros-ec behavior by displaying both the user types "<command> help",
	 */
#ifdef CONFIG_SHELL_HELP
	for (int i = 1; i < argc; i++) {
		if (!command->help && !command->argdesc)
			break;
		if (!strcmp(argv[i], "help")) {
			print_console_help(argv[0], command);
			return 0;
		}
	}
#endif

	ret = command->handler(argc, argv);
	if (ret == EC_SUCCESS)
		return ret;

	/* Print common parameter error conditions and help on error */
	if (ret >= EC_ERROR_PARAM1 && ret < EC_ERROR_PARAM_COUNT)
		printk("Parameter %d invalid\n", ret - EC_ERROR_PARAM1 + 1);
	else if (ret == EC_ERROR_PARAM_COUNT)
		printk("Wrong number of parameters\n");
	else
		printk("Command returned error: %d\n", ret);

#ifdef CONFIG_SHELL_HELP
	print_console_help(argv[0], command);
#endif
	return ret;
}

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(ec_console) <= 1,
	     "at most one ec-console compatible node may be present");

#define EC_CONSOLE_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(ec_console)
#if DT_NODE_EXISTS(EC_CONSOLE_NODE)

static const char *const disabled_channels[] =
	DT_PROP(EC_CONSOLE_NODE, disabled);
static const size_t disabled_channel_count =
	DT_PROP_LEN(EC_CONSOLE_NODE, disabled);
static int init_ec_console(void)
{
	for (size_t i = 0; i < disabled_channel_count; i++)
		console_channel_disable(disabled_channels[i]);

	return 0;
}
SYS_INIT(init_ec_console, PRE_KERNEL_1,
	 CONFIG_PLATFORM_EC_CONSOLE_INIT_PRIORITY);
#endif /* CONFIG_PLATFORM_EC_CONSOLE_CHANNEL */

#ifdef CONFIG_LOG_MODE_MINIMAL
static int zephyr_shim_console_out(int c)
{
	/* Always capture EC output into the AP console buffer. */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE) && !k_is_in_isr()) {
		char console_char = c;
		console_buf_notify_chars(&console_char, 1);
	}

	/*
	 * CC_ZEPHYR_LOG is a catchall for all output generated from the
	 * Zephyr printk() backend when using CONFIG_LOG_MODE_MINIMAL.
	 * No legacy cputs/cprints calls use this directly, but the "chan"
	 * console command can be used to turn Zephyr logging on and off.
	 */
	if (console_channel_is_disabled(CC_ZEPHYR_LOG)) {
		return c;
	}

	return zephyr_char_out(c);
}
#endif

static int init_ec_shell(void)
{
#if defined(CONFIG_SHELL_BACKEND_SERIAL)
	shell_zephyr = shell_backend_uart_get_ptr();
#elif defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
	shell_zephyr = shell_backend_dummy_get_ptr(); /* nocheck */
#else
#error A shell backend must be enabled
#endif

	/*
	 * Install our own printk handler if using LOG_MODE_MINIMAL.  This
	 * allows us to capture all character output and copy into the
	 * AP console buffer.
	 *
	 * For other other logging modes, projects should enable
	 * CONFIG_PLATFORM_EC_LOG_BACKEND_CONSOLE_BUFFER to capture log
	 * output into the AP console buffer.
	 */
#ifdef CONFIG_LOG_MODE_MINIMAL
	zephyr_char_out = __printk_get_hook();
	__printk_hook_install(zephyr_shim_console_out);
#endif

	return 0;
}
SYS_INIT(init_ec_shell, PRE_KERNEL_1, CONFIG_PLATFORM_EC_CONSOLE_INIT_PRIORITY);

#ifdef CONFIG_LOG_MODE_MINIMAL
BUILD_ASSERT(CONFIG_PLATFORM_EC_CONSOLE_INIT_PRIORITY >
		     CONFIG_CONSOLE_INIT_PRIORITY,
	     "The console shim must be initialized after the console.");

#ifdef CONFIG_POSIX_ARCH_CONSOLE
BUILD_ASSERT(CONFIG_PLATFORM_EC_CONSOLE_INIT_PRIORITY >
		     CONFIG_POSIX_ARCH_CONSOLE_INIT_PRIORITY,
	     "The console shim must be initialized after the posix console.");
#endif /* CONFIG_POSIX_ARCH_CONSOLE */
#endif /* CONFIG_LOG_MODE_MINIMAL */

#ifdef TEST_BUILD
const struct shell *get_ec_shell(void)
{
	return shell_zephyr;
}
#endif

k_tid_t get_shell_thread(void)
{
	if (shell_zephyr == NULL) {
		return NULL;
	}
	return shell_zephyr->thread;
}

void uart_tx_start(void)
{
}

int uart_tx_ready(void)
{
	return 1;
}

int uart_tx_char_raw(void *context, int c)
{
	uart_write_char(c);
	return 0;
}

void uart_write_char(char c)
{
	uart_poll_out(uart_shell_dev, c);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE) && !k_is_in_isr())
		console_buf_notify_chars(&c, 1);
}

void uart_flush_output(void)
{
	uart_tx_flush();
}

void uart_tx_flush(void)
{
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	while (!uart_irq_tx_complete(uart_shell_dev))
		;
#endif
}

int uart_getc(void)
{
	uint8_t c;
	int rv = -1;

	/*
	 * Don't try to read from the uart when the console
	 * owns it.
	 */
	if (!shell_stopped && !rx_bypass_enabled) {
		LOG_ERR("Shell must be stopped or rx bypass enabled");
		return -1;
	}

	if (IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN) || rx_bypass_enabled) {
		if (ring_buf_get(&rx_buffer, &c, 1)) {
			rv = c;
		}
	} else {
		rv = uart_poll_in(uart_shell_dev, &c);
		if (!rv) {
			rv = c;
		}
	}
	return rv;
}

void uart_clear_input(void)
{
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	/* Reset the input ring buffer */
	ring_buf_reset(&rx_buffer);
#endif
}

#ifndef CONFIG_PIGWEED_LOG_TOKENIZED_LIB
static void handle_sprintf_rv(int rv, size_t *len)
{
	if (rv < 0) {
		LOG_ERR("Print buffer is too small");
		*len = CONFIG_SHELL_PRINTF_BUFF_SIZE;
	} else {
		*len += rv;
	}
}

static void zephyr_print(const char *buff, size_t size, bool is_shell_output)
{
	/*
	 * shell_* functions can not be used in ISRs so optionally use
	 * printk instead.
	 * If the shell is about to be (or is) stopped, use printk, since the
	 * output may be stalled and the shell mutex held.
	 * Also, console_buf_notify_chars uses a mutex, which may not be
	 * locked in ISRs.
	 */
	bool in_isr = k_is_in_isr();

	if (in_isr || shell_stopped ||
	    shell_zephyr->ctx->state != SHELL_STATE_ACTIVE) {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_ISR_CONSOLE_OUTPUT) ||
		    !in_isr) {
			printk("!%s", buff);
			return;
		}
	}

	if (is_shell_output) {
		/* Always send CC_COMMAND tagged output directly to the shell.
		 * This also skips sending console command output to the AP
		 * console buffer.
		 */
		shell_fprintf(shell_zephyr, SHELL_NORMAL, "%s", buff);
	} else if (IS_ENABLED(CONFIG_LOG_MODE_MINIMAL)) {
		/*
		 * The shell UART backend uses uart_fifo_fill() while
		 * the LOG_MODE_MINIMAL uses printk() and calls
		 * uart_poll_out().
		 *
		 * When LOG_MODE_MINIMAL enabled, send all output
		 * to the logging subsystem to minimize mixing output
		 * messages.  AP console buffer is handled above
		 * with a custom printk hook.
		 */
		LOG_RAW("%s", buff);
	} else {
		/*
		 * LOGGING disabled, or uses a mode besides
		 * CONFIG_LOG_MODE_MINIMAL. Send the output to the shell
		 * backend and also copy in to the AP console buffer.
		 */
		shell_fprintf(shell_zephyr, SHELL_NORMAL, "%s", buff);
		if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE)) {
			console_buf_notify_chars(buff, size);
		}
	}
	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_DEBUG)) {
		printk("%s", buff);
	}
}
#endif /* CONFIG_PIGWEED_LOG_TOKENIZED_LIB */

#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)
BUILD_ASSERT(0, "USB console is not supported with Zephyr");
#endif /* defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM) */

#ifndef CONFIG_PIGWEED_LOG_TOKENIZED_LIB
int cputs(enum console_channel channel, const char *outstr)
{
	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	zephyr_print(outstr, strlen(outstr),
		     channel == CC_COMMAND ? true : false);

	return 0;
}

int cvprintf(enum console_channel channel, const char *format, va_list args)
{
	int rv;
	size_t len = 0;
	char buff[CONFIG_SHELL_PRINTF_BUFF_SIZE];

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	rv = crec_vsnprintf(buff, CONFIG_SHELL_PRINTF_BUFF_SIZE, format, args);
	handle_sprintf_rv(rv, &len);

	zephyr_print(buff, len, channel == CC_COMMAND ? true : false);

	return rv > 0 ? EC_SUCCESS : rv;
}

int cprintf(enum console_channel channel, const char *format, ...)
{
	int rv;
	va_list args;

	va_start(args, format);
	rv = cvprintf(channel, format, args);
	va_end(args);

	return rv;
}

int cvprints(enum console_channel channel, const char *format, va_list args)
{
	int rv;
	char buff[CONFIG_SHELL_PRINTF_BUFF_SIZE];
	size_t len = 0;

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	buff[0] = '[';
	len = 1;

	rv = snprintf_timestamp_now(buff + len, sizeof(buff) - len);
	handle_sprintf_rv(rv, &len);

	rv = crec_snprintf(buff + len, CONFIG_SHELL_PRINTF_BUFF_SIZE - len,
			   " ");
	handle_sprintf_rv(rv, &len);

	rv = crec_vsnprintf(buff + len, CONFIG_SHELL_PRINTF_BUFF_SIZE - len,
			    format, args);
	handle_sprintf_rv(rv, &len);

	rv = crec_snprintf(buff + len, CONFIG_SHELL_PRINTF_BUFF_SIZE - len,
			   "]\n");
	handle_sprintf_rv(rv, &len);

	zephyr_print(buff, len, channel == CC_COMMAND ? true : false);

	return rv > 0 ? EC_SUCCESS : rv;
}

int cprints(enum console_channel channel, const char *format, ...)
{
	int rv;
	va_list args;

	va_start(args, format);
	rv = cvprints(channel, format, args);
	va_end(args);

	return rv;
}
#endif /* CONFIG_PIGWEED_LOG_TOKENIZED_LIB */

#ifdef CONFIG_PLATFORM_EC_LOG_CUSTOM_TIMESTAMP
static int custom_timestamp(const struct log_output *output,
			    const log_timestamp_t timestamp,
			    const log_timestamp_printer_t printer)
{
	uint64_t us = log_output_timestamp_to_us(timestamp);

	return printer(output, "[%" PRIu32 ".%06" PRIu32 "] ",
		       (uint32_t)(us / USEC_PER_SEC),
		       (uint32_t)(us % USEC_PER_SEC));
}

static int timestamp_init(void)
{
	log_custom_timestamp_set(custom_timestamp);

	return 0;
}
SYS_INIT(timestamp_init, POST_KERNEL, CONFIG_LOG_CORE_INIT_PRIORITY);
#endif /* CONFIG_PLATFORM_EC_LOG_CUSTOM_TIMESTAMP */
