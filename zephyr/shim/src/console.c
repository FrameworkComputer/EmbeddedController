/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>
#ifdef CONFIG_SHELL_BACKEND_DUMMY /* nocheck */
#include <zephyr/shell/shell_dummy.h> /* nocheck */
#endif
#include <zephyr/shell/shell_uart.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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

#if !defined(CONFIG_SHELL_BACKEND_SERIAL) && \
	!defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
#error Must select either CONFIG_SHELL_BACKEND_SERIAL or \
	CONFIG_SHELL_BACKEND_DUMMY /* nocheck */
#endif
#if defined(CONFIG_SHELL_BACKEND_SERIAL) && \
	defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
#error Must select only one shell backend
#endif

BUILD_ASSERT(EC_TASK_PRIORITY(EC_SHELL_PRIO) == CONFIG_SHELL_THREAD_PRIORITY,
	     "EC_SHELL_PRIO does not match CONFIG_SHELL_THREAD_PRIORITY.");

LOG_MODULE_REGISTER(shim_console, LOG_LEVEL_ERR);

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

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
RING_BUF_DECLARE(rx_buffer, CONFIG_UART_RX_BUF_SIZE);

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

static void uart_callback(const struct device *dev, void *user_data)
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

	/*
	 * shell_init() always resets the priority back to the default.
	 * Update the priority as setup by the shimmed task code.
	 */
	k_thread_priority_set(shell_zephyr->ctx->tid,
			      EC_TASK_PRIORITY(EC_SHELL_PRIO));

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

#if defined(CONFIG_CONSOLE_CHANNEL) && DT_NODE_EXISTS(DT_PATH(ec_console))
#define EC_CONSOLE DT_PATH(ec_console)

static const char *const disabled_channels[] = DT_PROP(EC_CONSOLE, disabled);
static const size_t disabled_channel_count = DT_PROP_LEN(EC_CONSOLE, disabled);
static int init_ec_console(const struct device *unused)
{
	for (size_t i = 0; i < disabled_channel_count; i++)
		console_channel_disable(disabled_channels[i]);

	return 0;
}
SYS_INIT(init_ec_console, PRE_KERNEL_1, 50);
#endif /* CONFIG_CONSOLE_CHANNEL && DT_NODE_EXISTS(DT_PATH(ec_console)) */

static int init_ec_shell(const struct device *unused)
{
#if defined(CONFIG_SHELL_BACKEND_SERIAL)
	shell_zephyr = shell_backend_uart_get_ptr();
#elif defined(CONFIG_SHELL_BACKEND_DUMMY) /* nocheck */
	shell_zephyr = shell_backend_dummy_get_ptr(); /* nocheck */
#else
#error A shell backend must be enabled
#endif
	return 0;
}
SYS_INIT(init_ec_shell, PRE_KERNEL_1, 50);

#ifdef TEST_BUILD
const struct shell *get_ec_shell(void)
{
	return shell_zephyr;
}
#endif

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

	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE))
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
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	uint8_t c;

	if (ring_buf_get(&rx_buffer, &c, 1)) {
		return c;
	}
	return -1;
#else
	uint8_t c;
	int rv;

	rv = uart_poll_in(uart_shell_dev, &c);
	if (rv) {
		return rv;
	}
	return c;
#endif
}

void uart_clear_input(void)
{
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	/* Reset the input ring buffer */
	ring_buf_reset(&rx_buffer);
#endif
}

static void handle_sprintf_rv(int rv, size_t *len)
{
	if (rv < 0) {
		LOG_ERR("Print buffer is too small");
		*len = CONFIG_SHELL_PRINTF_BUFF_SIZE;
	} else {
		*len += rv;
	}
}

static void zephyr_print(const char *buff, size_t size)
{
	/*
	 * shell_* functions can not be used in ISRs so use printk instead.
	 * If the shell is about to be (or is) stopped, use printk, since the
	 * output may be stalled and the shell mutex held.
	 * Also, console_buf_notify_chars uses a mutex, which may not be
	 * locked in ISRs.
	 */
	if (k_is_in_isr() || shell_stopped ||
	    shell_zephyr->ctx->state != SHELL_STATE_ACTIVE) {
		printk("%s", buff);
	} else {
		shell_fprintf(shell_zephyr, SHELL_NORMAL, "%s", buff);
		if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE))
			console_buf_notify_chars(buff, size);
		if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_DEBUG))
			printk("%s", buff);
	}
}

#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)
BUILD_ASSERT(0, "USB console is not supported with Zephyr");
#endif /* defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM) */

int cputs(enum console_channel channel, const char *outstr)
{
	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	zephyr_print(outstr, strlen(outstr));

	return 0;
}

int cprintf(enum console_channel channel, const char *format, ...)
{
	int rv;
	va_list args;
	size_t len = 0;
	char buff[CONFIG_SHELL_PRINTF_BUFF_SIZE];

	/* Filter out inactive channels */
	if (console_channel_is_disabled(channel))
		return EC_SUCCESS;

	va_start(args, format);
	rv = crec_vsnprintf(buff, CONFIG_SHELL_PRINTF_BUFF_SIZE, format, args);
	va_end(args);
	handle_sprintf_rv(rv, &len);

	zephyr_print(buff, len);

	return rv > 0 ? EC_SUCCESS : rv;
}

int cprints(enum console_channel channel, const char *format, ...)
{
	int rv;
	va_list args;
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

	va_start(args, format);
	rv = crec_vsnprintf(buff + len, CONFIG_SHELL_PRINTF_BUFF_SIZE - len,
			    format, args);
	va_end(args);
	handle_sprintf_rv(rv, &len);

	rv = crec_snprintf(buff + len, CONFIG_SHELL_PRINTF_BUFF_SIZE - len,
			   "]\n");
	handle_sprintf_rv(rv, &len);

	zephyr_print(buff, len);

	return rv > 0 ? EC_SUCCESS : rv;
}
