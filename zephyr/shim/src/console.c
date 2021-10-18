/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/uart.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <stdbool.h>
#include <string.h>
#include <sys/printk.h>
#include <sys/ring_buffer.h>
#include <zephyr.h>
#include <logging/log.h>

#include "console.h"
#include "printf.h"
#include "uart.h"
#include "usb_console.h"
#include "zephyr_console_shim.h"

LOG_MODULE_REGISTER(shim_console, LOG_LEVEL_ERR);

static const struct device *uart_shell_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
static const struct shell *shell_zephyr;
static struct k_poll_signal shell_uninit_signal;
static struct k_poll_signal shell_init_signal;
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

static void shell_uninit_callback(const struct shell *shell, int res)
{
	if (!res) {
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
	}

	/* Notify the uninit signal that we finished */
	k_poll_signal_raise(&shell_uninit_signal, res);
}

int uart_shell_stop(void)
{
	struct k_poll_event event = K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
		&shell_uninit_signal);

	/* Clear all pending input */
	uart_clear_input();

	/* Disable RX and TX interrupts */
	uart_irq_rx_disable(uart_shell_dev);
	uart_irq_tx_disable(uart_shell_dev);

	/* Initialize the uninit signal */
	k_poll_signal_init(&shell_uninit_signal);

	/* Stop the shell */
	shell_uninit(shell_backend_uart_get_ptr(), shell_uninit_callback);

	/* Wait for the shell to be turned off, the signal will wake us */
	k_poll(&event, 1, K_FOREVER);

	/* Event was signaled, return the result */
	return event.signal->result;
}

static void shell_init_from_work(struct k_work *work)
{
	bool log_backend = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > 0;
	uint32_t level;
	ARG_UNUSED(work);

	if (CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > LOG_LEVEL_DBG) {
		level = CONFIG_LOG_MAX_LEVEL;
	} else {
		level = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL;
	}

	/* Initialize the shell and re-enable both RX and TX */
	shell_init(shell_backend_uart_get_ptr(), uart_shell_dev, false,
		   log_backend, level);
	uart_irq_rx_enable(uart_shell_dev);
	uart_irq_tx_enable(uart_shell_dev);

	/* Notify the init signal that initialization is complete */
	k_poll_signal_raise(&shell_init_signal, 0);
}

void uart_shell_start(void)
{
	static struct k_work shell_init_work;
	struct k_poll_event event = K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
		&shell_init_signal);

	/* Disable RX and TX interrupts */
	uart_irq_rx_disable(uart_shell_dev);
	uart_irq_tx_disable(uart_shell_dev);

	/* Initialize k_work to call shell init (this makes it thread safe) */
	k_work_init(&shell_init_work, shell_init_from_work);

	/* Initialize the init signal to make sure we're read to listen */
	k_poll_signal_init(&shell_init_signal);

	/* Submit the work to be run by the kernel */
	k_work_submit(&shell_init_work);

	/* Wait for initialization to be run, the signal will wake us */
	k_poll(&event, 1, K_FOREVER);
}

int zshim_run_ec_console_command(const struct zephyr_console_command *command,
				 size_t argc, char **argv)
{
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
			if (command->help)
				printk("%s\n", command->help);
			if (command->argdesc)
				printk("Usage: %s\n", command->argdesc);
			return 0;
		}
	}
#endif

	return command->handler(argc, argv);
}

#if defined(CONFIG_CONSOLE_CHANNEL) && DT_NODE_EXISTS(DT_PATH(ec_console))
#define EC_CONSOLE DT_PATH(ec_console)

static const char * const disabled_channels[] = DT_PROP(EC_CONSOLE, disabled);
static const size_t disabled_channel_count = DT_PROP_LEN(EC_CONSOLE, disabled);
static int init_ec_console(const struct device *unused)
{
	for (size_t i = 0; i < disabled_channel_count; i++)
		console_channel_disable(disabled_channels[i]);

	return 0;
} SYS_INIT(init_ec_console, PRE_KERNEL_1, 50);
#endif /* CONFIG_CONSOLE_CHANNEL && DT_NODE_EXISTS(DT_PATH(ec_console)) */

static int init_ec_shell(const struct device *unused)
{
	shell_zephyr = shell_backend_uart_get_ptr();
	return 0;
} SYS_INIT(init_ec_shell, PRE_KERNEL_1, 50);

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
	printk("%c", c);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE))
		console_buf_notify_chars(&c, 1);
}

void uart_flush_output(void)
{
	shell_process(shell_zephyr);
	uart_tx_flush();
}

void uart_tx_flush(void)
{
	while (!uart_irq_tx_complete(uart_shell_dev))
		;
}

int uart_getc(void)
{
	uint8_t c;

	if (ring_buf_get(&rx_buffer, &c, 1)) {
		return c;
	}
	return -1;
}

void uart_clear_input(void)
{
	/* Clear any remaining shell processing. */
	shell_process(shell_zephyr);
	ring_buf_reset(&rx_buffer);
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
	 * Also, console_buf_notify_chars uses a mutex, which may not be
	 * locked in ISRs.
	 */
	if (k_is_in_isr() || shell_zephyr->ctx->state != SHELL_STATE_ACTIVE) {
		printk("%s", buff);
	} else {
		shell_fprintf(shell_zephyr, SHELL_NORMAL, "%s", buff);
		if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE))
			console_buf_notify_chars(buff, size);
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

	rv = crec_snprintf(buff, CONFIG_SHELL_PRINTF_BUFF_SIZE, "[%pT ",
			 PRINTF_TIMESTAMP_NOW);
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
