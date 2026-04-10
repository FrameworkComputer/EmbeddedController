/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "power.h"

#include <zephyr/kernel.h>

#include <subsys/usbd_service.h>

#ifdef CONFIG_PLATFORM_EC_SOC_IT8XXX2_CONSOLE_BUF_H2RAM_SHARED
extern uint8_t h2ram_pool[];
#define console_buf h2ram_pool
/*
 * CONFIG_ESPI_PERIPHERAL_HOST_CMD_PARAM_PORT_NUM and
 * CONFIG_ESPI_PERIPHERAL_ACPI_SHM_REGION_PORT_NUM represent the offset from the
 * base of h2ram_pool where each eSPI region begins.
 * Since console_buf is placed at the beginning of h2ram_pool, ensure its size
 * does not exceed these offsets so that it does not overlap with the
 * host-command parameter region or ACPI shared memory region.
 */
BUILD_ASSERT(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE <=
	     CONFIG_ESPI_PERIPHERAL_HOST_CMD_PARAM_PORT_NUM);
BUILD_ASSERT(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE <=
	     CONFIG_ESPI_PERIPHERAL_ACPI_SHM_REGION_PORT_NUM);
#else
static char console_buf[CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE];
#endif
static uint32_t previous_snapshot_idx;
static uint32_t current_snapshot_idx;
static uint32_t read_next_idx;
static uint32_t head_idx;
static uint32_t tail_idx;
#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
/* Guarded with console_write_lock */
static bool new_console_log;

void console_log_notify_host(char new_char)
{
	/* Inform about every new line, not to send event every char. */
	if (new_char == '\n') {
		/* Avoid waking the host from a low-power state for console
		 * logs.
		 */
		bool wake_host = true;
#ifdef CONFIG_AP_POWER_CONTROL
		if (power_get_state() != POWER_S0) {
			wake_host = false;
		}
#endif /* CONFIG_AP_POWER_CONTROL */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_USB
		if (usb_is_suspended()) {
			wake_host = false;
		}
#endif /* CONFIG_EC_HOST_CMD_BACKEND_USB */
		if (!new_console_log && wake_host) {
			/* Set the host event once since last snapshot not to
			 * spam events e.g. in case host doesn't read the logs.
			 */
			new_console_log = true;
			host_set_single_event(EC_HOST_EVENT_CONSOLE_LOGS);
		}
	}
}
#endif

static inline uint32_t next_idx(uint32_t cur_idx)
{
	return (cur_idx + 1) % CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE;
}

K_MUTEX_DEFINE(console_write_lock);

size_t console_buf_notify_chars(const char *s, size_t len)
{
	/*
	 * This is just notifying of console characters for debugging
	 * output, so if we are unable to lock the mutex immediately,
	 * then just drop the string. Mutexes cannot be locked from an
	 * isr, so also drop the string in this case too.
	 */
	if (k_is_in_isr() || k_mutex_lock(&console_write_lock, K_NO_WAIT))
		return 0;
	/* We got the mutex. */
	for (size_t i = 0; i < len; i++) {
		/* Don't copy null byte into buffer */
		if (!(*s)) {
			s++;
			continue;
		}

		uint32_t new_tail = next_idx(tail_idx);

		/* Check if we are starting to overwrite our snapshot
		 * heads
		 */
		if (new_tail == head_idx)
			head_idx = next_idx(head_idx);
		if (new_tail == previous_snapshot_idx)
			previous_snapshot_idx = next_idx(previous_snapshot_idx);
		if (new_tail == current_snapshot_idx)
			current_snapshot_idx = next_idx(current_snapshot_idx);
		if (new_tail == read_next_idx)
			read_next_idx = next_idx(read_next_idx);

		console_buf[tail_idx] = *s++;

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
		console_log_notify_host(console_buf[tail_idx]);
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT */

		tail_idx = new_tail;
	}
	k_mutex_unlock(&console_write_lock);
	return len;
}

enum ec_status uart_console_read_buffer_init(void)
{
	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

	/* For read next, start reading at the beginning of the buffer */
	read_next_idx = head_idx;
	/*
	 * For read recent, start reading at the beginning of the previous
	 * snapshot
	 */
	previous_snapshot_idx = current_snapshot_idx;
	/*
	 * Limit read command to characters available at the moment of creating
	 * snapshot
	 */
	current_snapshot_idx = tail_idx;

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT
	new_console_log = false;
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_CONSOLE_LOGS));
#endif /* CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_EVENT */

	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

int uart_console_read_buffer(uint8_t type, char *dest, uint16_t dest_size,
			     uint16_t *write_count_out)
{
	uint32_t *head;
	uint16_t write_count = 0;

	switch (type) {
	case CONSOLE_READ_NEXT:
		/*
		 * Start where we left or from the beginning of the buffer after
		 * snapshot
		 */
		head = &read_next_idx;
		break;
	case CONSOLE_READ_RECENT:
		/* Start from end of previous snapshot */
		head = &previous_snapshot_idx;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* We need to make sure we have room for at least the null byte */
	if (dest_size == 0)
		return EC_RES_INVALID_PARAM;

	if (k_mutex_lock(&console_write_lock, K_MSEC(100)))
		/* Failed to acquire console buffer mutex */
		return EC_RES_TIMEOUT;

	if (*head == current_snapshot_idx) {
		/* No new data, return empty response */
		k_mutex_unlock(&console_write_lock);
		*write_count_out = 0;
		return EC_RES_SUCCESS;
	}

	do {
		if (write_count >= dest_size - 1)
			/* Buffer is full, minus the space for a null byte */
			break;

		dest[write_count] = console_buf[*head];
		write_count++;
		*head = next_idx(*head);
	} while (*head != current_snapshot_idx);

	dest[write_count] = '\0';
	write_count++;

	*write_count_out = write_count;
	k_mutex_unlock(&console_write_lock);

	return EC_RES_SUCCESS;
}

/* ECOS uart buffer, putc is blocking instead. */
int uart_buffer_full(void)
{
	return false;
}
