/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "crc8.h"
#include "flash_log.h"
#include "flash.h"
#include "hooks.h"
#include "shared_mem.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * A few assumptions this log facility design is based on are:
 *
 * - the log is stored in a flash space configured per board/chip combination.
 *   Chip level physical access functions are used for writing and erasing.
 *
 * - flash space access control is transparent for the log facility, if
 *   necessary, chip driver can register a callback for flash access control.
 *
 * - log events are rare, attempts to log concurrent events could fail.
 *
 * - log events are retrieved by the host periodically, much sooner than log
 *   overflows
 *
 * - as presented this facility is not suitable for saving panics'
 *   information, because flash drivers usually require OS services like
 *   interrupts, events, etc.
 *
 * - at the point of logging an entry there is still 200 bytes or so of stack
 *   is available.
 *
 * With the above in mind, here is the basic design:
 *
 * Entries in the log are of variable size, this layer is completely oblivious
 * to the entries' contents. Each entry is saved in the log prepended by a
 * header, which includes the following fields:
 *
 * - entry type, 1 byte
 * - the timestamp the entry is saved at, 4 bytes, if real time is not
 *   available a monotonously increasing number is used
 * - entry size, one byte, size is limited to 63 bytes maximum, two top bits
 *   of the size byte could be used as flags.
 * - the entry crc, 1 byte
 *
 * To satisfy flash access limitations, this facility pads log entries to a
 * multiple of the physical flash write size. Padding bytes value is set to
 * FE_LOG_PAD. Having a fixed padding value will make it easier to examine log
 * space snapshots by third party software. The users of this service are
 * oblivious to the padding, they write and read back entries of arbitrary not
 * necessarily aligned sizes in 0..MAX_FLASH_LOG_PAYLOAD_SIZE range.
 *
 * The log is kept in one flash page. Entries are of variable size, as defined
 * by entry header. For read accesses log is mapped directly into the address
 * space, write accesses are handled by chip specific drivers.
 *
 * On each startup, if the log is more than three quarters full, the log flash
 * space is erased and a quarter space worth from top of the log is written
 * back at the bottom of the erased space.
 *
 * If an entry would not fit into the log it is silently dropped.
 *
 * Log entries can not be written or read from within interrupt processing
 * routines.
 *
 * Only one read or write access can be in progress at a time. Attempts to log
 * new events while a log entry is being saved or retrieved will be ignored.
 * Attempts to retrieve an entry while another entry is being saved or
 * retrieved will return the appropriate return value.
 *
 * At run time log compaction is attempted if a request to add an entry is
 * made and the log is more than 90% full. If compaction is not possible (for
 * example, if memory allocation fails) and the new entry does not fit, it
 * would be dropped.
 *
 * The above mentioned failures are tracked and when log becomes operational
 * again (for instance memory heap grew back), log entries are added to record
 * previously encountered failures.
 *
 * The API to retrieve log entries gets the timestamp of the last retrieved
 * entry as an input parameter and returns the next entry exists. Sequence of
 * invocations of the log entry retrieval API starting with timestamp of zero
 * and then repeating with the timestamp of the previously retrieved entry
 * allows to traverse the entire log.
 *
 * Initialization function verifies log integrity. When initializing from an
 * erased space, the initialization function saves a new entry of type
 * FE_LOG_START. If log corruption is detected, the initialization function
 * tries to compact the log and adds a new entry of type FE_LOG_CORRUPTED on
 * top of the compacted log.
 */

/*
 * Structure keeping the context of the last entry retrieval access. If the
 * next retrieval passed in timestamp saved in prev_timestamp, log search
 * starts at read_cursor.
 */
static struct {
	uint16_t read_cursor;
	uint32_t prev_timestamp;
} log_read_context;

/* Location where next entry is going to be added. */
static uint16_t log_write_cursor;
/*
 * Base time in seconds, during init set to the time of the latest present log
 * entry +1, expected be set by the host to current time. Log entries
 * timestamps are set to this value plus uptime.
 */
static uint32_t log_tstamp_base;

/*
 * Keep track of the last used timestamp value to make sure there are no two
 * entries with the same timestamp.
 */
test_mockable_static uint32_t last_used_timestamp;

/* Set to True after log has been initialized. */
static uint8_t log_inited;
test_mockable_static uint8_t log_event_in_progress;
test_mockable_static uint32_t lock_failures_count;
static uint32_t overflow_failures_count;

/*
 * Callback set by the chip if flash log space access requires additional
 * access control.
 */
static void (*platform_flash_control)(int enable);

/*
 * Helper function, convert offset in the log into a physical address in
 * flash.
 */
static const void *log_offset_to_addr(uint16_t log_offset)
{
	return (const void *)(CONFIG_FLASH_LOG_BASE + log_offset);
}

/* Wrappers around chip flash access functions. */
static void flash_log_erase(void)
{
	flash_physical_erase(CONFIG_FLASH_LOG_BASE - CONFIG_PROGRAM_MEMORY_BASE,
			     CONFIG_FLASH_LOG_SPACE);
}

static void flash_log_write(uint16_t log_offset, const void *data,
			    size_t data_size)
{
	flash_physical_write(log_offset + CONFIG_FLASH_LOG_BASE -
				     CONFIG_PROGRAM_MEMORY_BASE,
			     data_size, data);
}

/* Wrappers around platform flash control function, if registered. */
static void flash_log_write_enable(void)
{
	if (platform_flash_control)
		platform_flash_control(1);
}

static void flash_log_write_disable(void)
{
	if (platform_flash_control)
		platform_flash_control(0);
}

/*
 * Wrapper around crc8 calculation to avoid excessive typecasting throughout
 * the rest of the file.
 */
static uint8_t calc_crc8(const void *buf, size_t size, uint8_t prev)
{
	return crc8_arg((const uint8_t *)buf, size, prev);
}

/* Try grabbing the lock, non blocking, return True if succeeded. */
static int flash_log_lock_successful(void)
{
	if (in_interrupt_context())
		return 0;

	if (!log_inited)
		return 0;

	interrupt_disable();
	if (log_event_in_progress) {
		/* What a coincidence! */
		interrupt_enable();
		return 0;
	}
	log_event_in_progress = 1;
	interrupt_enable();
	return 1;
}

/*
 * Verify entry validity, i.e. that it does fit into the log, has size within
 * range and its crc8 matches.
 */
static int entry_is_valid(const struct flash_log_entry *r)
{
	size_t entry_size;
	uint32_t entry_offset;
	struct flash_log_entry copy;

	entry_size = FLASH_LOG_ENTRY_SIZE(r->size);
	entry_offset = (uintptr_t)r - CONFIG_FLASH_LOG_BASE;

	if ((entry_offset + entry_size) > CONFIG_FLASH_LOG_SPACE)
		return 0;

	/* Crc of the entry is calculated with the crc field set to zero. */
	copy = *r;
	copy.crc = 0;
	copy.crc = calc_crc8(&copy, sizeof(copy), 0);
	copy.crc = calc_crc8(r + 1, FLASH_LOG_PAYLOAD_SIZE(r->size), copy.crc);
	return (copy.crc == r->crc);
}

/* Attempt compacting the log. Could fail if memory allocation fails. */
static void try_compacting(void)
{
	char *buf;
	uint16_t read_cursor = 0;
	uint16_t compac_cursor = 0;

	/* Try rewriting the top 25% of the log into its bottom. */
	/*
	 * Fist allocate a buffer large enough to keep a quarter of the
	 * log.
	 */
	if (shared_mem_acquire(COMPACTION_SPACE_PRESERVE, &buf) != EC_SUCCESS)
		return;

	while (read_cursor < log_write_cursor) {
		const struct flash_log_entry *r;
		size_t entry_space;

		r = log_offset_to_addr(read_cursor);
		if (!entry_is_valid(r))
			break;

		entry_space = FLASH_LOG_ENTRY_SIZE(r->size);

		if ((log_write_cursor - read_cursor) <=
		    COMPACTION_SPACE_PRESERVE) {
			memcpy(buf + compac_cursor, r, entry_space);
			compac_cursor += entry_space;
		}

		read_cursor += entry_space;
	}

	flash_log_write_enable();
	flash_log_erase();
	flash_log_write(0, buf, compac_cursor);
	log_write_cursor = compac_cursor;
	flash_log_write_disable();

	shared_mem_release(buf);

	log_read_context.read_cursor = 0;
	log_read_context.prev_timestamp = 0;
}

static enum ec_error_list flash_log_add_event_core(uint8_t type, uint8_t size,
						   void *payload)
{
	union entry_u e;
	size_t padded_entry_size;
	size_t entry_size;
	enum ec_error_list rv = EC_ERROR_INVAL;
	uint32_t new_timestamp;

	if (size > MAX_FLASH_LOG_PAYLOAD_SIZE)
		return rv;

	if (!flash_log_lock_successful()) {
		lock_failures_count++;
		return rv;
	}

	/* The entry will take this much space in the flash. */
	padded_entry_size = FLASH_LOG_ENTRY_SIZE(size);

	if (log_write_cursor > RUN_TIME_LOG_FULL_WATERMARK)
		try_compacting();

	if (padded_entry_size > (CONFIG_FLASH_LOG_SPACE - log_write_cursor)) {
		/*
		 * Compaction must have failed or was not allowed, and no room
		 * to log.
		 */
		overflow_failures_count++;
		goto log_add_exit;
	}

	/* Copy the payload into the entry if necessary. */
	if (size)
		memcpy(e.r.payload, payload, size);

	entry_size = sizeof(e.r) + size;

	new_timestamp = flash_log_get_tstamp();

	/*
	 * Avoid rolling back or logging more than one entry with the same
	 * timestamp.
	 */
	if (last_used_timestamp >= new_timestamp)
		last_used_timestamp += 1;
	else
		last_used_timestamp = new_timestamp;

	e.r.timestamp = last_used_timestamp;
	e.r.size = size;
	e.r.type = type;
	e.r.crc = 0;
	e.r.crc = calc_crc8(e.entry, entry_size, 0);

	/* Add padding if necessary. */
	while (entry_size < padded_entry_size)
		e.entry[entry_size++] = FE_LOG_PAD;

	flash_log_write_enable();
	flash_log_write(log_write_cursor, e.entry, padded_entry_size);
	flash_log_write_disable();

	log_write_cursor += padded_entry_size;

	rv = EC_SUCCESS;

log_add_exit:
	log_event_in_progress = 0;

	return rv;
}

/*
 * Report the failure count, using the passed in type. If report attempt is
 * successful, reset the counter.
 *
 * Even though the counter is 4 bytes in size, the log entry payload is a one
 * byte value capped at 255: the failure counter is extremely unlikely to
 * exceed this value, and if it does - we don't really care about the exact
 * number.
 */
static void report_failure(enum flash_event_type type, uint32_t *counter)
{
	uint8_t reported_counter;

	/*
	 * Let's truncate the value at one byte, it is extremely unlikely to
	 * exceed it.
	 */
	reported_counter = *counter;
	if (reported_counter > 255)
		reported_counter = 255;

	if (flash_log_add_event_core(type, sizeof(reported_counter),
				     &reported_counter) == EC_SUCCESS)
		*counter = 0;
}

void flash_log_add_event(uint8_t type, uint8_t size, void *payload)
{
	if (lock_failures_count)
		report_failure(FE_LOG_LOCKS, &lock_failures_count);

	if (overflow_failures_count)
		report_failure(FE_LOG_OVERFLOWS, &overflow_failures_count);

	flash_log_add_event_core(type, size, payload);
}

int flash_log_dequeue_event(uint32_t event_after, void *buffer,
			    size_t buffer_size)
{
	const struct flash_log_entry *r;
	int rv = 0;
	size_t copy_size;

	if (!flash_log_lock_successful())
		return -EC_ERROR_BUSY;

	if (!event_after || (event_after < log_read_context.prev_timestamp)) {
		/* Will have to start over. */
		log_read_context.read_cursor = 0;
		log_read_context.prev_timestamp = 0;
	}

	if (log_read_context.read_cursor >
	    (CONFIG_FLASH_LOG_SPACE - sizeof(*r)))
		/* No more room in the log. */
		goto log_read_exit;

	do {
		r = log_offset_to_addr(log_read_context.read_cursor);
		if (r->timestamp == CONFIG_FLASH_ERASED_VALUE32)
			/* Points at erased space, no more entries. */
			goto log_read_exit;

		if (!entry_is_valid(r)) {
			rv = -EC_ERROR_INVAL;
			goto log_read_exit;
		}

		log_read_context.read_cursor += FLASH_LOG_ENTRY_SIZE(r->size);

	} while (r->timestamp <= event_after);

	/*
	 * If we are here, we found the next event, let's see if it fits into
	 * the buffer.
	 */
	copy_size = FLASH_LOG_PAYLOAD_SIZE(r->size) + sizeof(*r);
	if (copy_size > buffer_size) {
		rv = -EC_ERROR_MEMORY_ALLOCATION;
		/* To be on the safe side will start over next time. */
		log_read_context.read_cursor = 0;
		log_read_context.prev_timestamp = 0;
		goto log_read_exit;
	}

	log_read_context.prev_timestamp = r->timestamp;
	memcpy(buffer, r, copy_size);
	rv = copy_size;

log_read_exit:
	log_event_in_progress = 0;
	return rv;
}

void flash_log_register_flash_control_callback(
	void (*flash_control)(int enable))
{
	platform_flash_control = flash_control;
}

test_export_static void flash_log_init(void)
{
	uint16_t read_cursor = 0;
	const struct flash_log_entry *r;

	r = log_offset_to_addr(read_cursor);
	while (entry_is_valid(r)) {
		last_used_timestamp = r->timestamp;
		read_cursor += FLASH_LOG_ENTRY_SIZE(r->size);
		r = log_offset_to_addr(read_cursor);
	}

	/* Should be updated by the AP soon after booting. */
	log_tstamp_base = last_used_timestamp + 1;

	log_write_cursor = read_cursor;
	log_inited = 1;

	flash_log_write_enable();
	if (r->timestamp != CONFIG_FLASH_ERASED_VALUE32) {
		/* Log space must be corrupted, compact it. */
		try_compacting();
		flash_log_add_event(FE_LOG_CORRUPTED, 0, NULL);
		flash_log_write_disable();
		return;
	}

	/*
	 * Timestamp field is set to all ones, presumably this points to free
	 * space in the log.
	 *
	 * Is there anything at all in the log?
	 */
	if (read_cursor) {
		/*
		 * Next write will have to come here unless compacting changes
		 * that.
		 */
		if (read_cursor > STARTUP_LOG_FULL_WATERMARK)
			try_compacting();
	} else {
		flash_log_add_event(FE_LOG_START, 0, NULL);
	}
	flash_log_write_disable();
}
DECLARE_HOOK(HOOK_INIT, flash_log_init, HOOK_PRIO_DEFAULT);

uint32_t flash_log_get_tstamp(void)
{
	return log_tstamp_base + get_time().val/1000000;
}

enum ec_error_list flash_log_set_tstamp(uint32_t tstamp)
{
	if (tstamp <= last_used_timestamp)
		return EC_ERROR_INVAL;

	log_tstamp_base = tstamp - get_time().val/1000000;

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_FLASH_LOG
/*
 * Display Flash event log.
 */
static int command_flash_log(int argc, char **argv)
{
	uint32_t stamp = 0;
	union entry_u e;
	int rv;
	uint32_t type;
	size_t size;
	size_t i;

	if (argc > 1) {
		if (!strcasecmp(argv[1], "-e")) {
			ccprintf("Erasing flash log\n");
			flash_log_write_enable();
			flash_log_erase();
			flash_log_write_disable();

			log_read_context.read_cursor = 0;
			log_read_context.prev_timestamp = 0;
			log_write_cursor = 0;

			argc--;
			argv++;
		}
	}
	if (argc < 3) {
		if (argc == 2)
			stamp = atoi(argv[1]);

		/* Retrieve entries newer than 'stamp'. */
		while ((rv = flash_log_dequeue_event(stamp, e.entry,
						     sizeof(e))) > 0) {
			size_t i;

			ccprintf("%10u:%02x", e.r.timestamp, e.r.type);
			for (i = 0; i < FLASH_LOG_PAYLOAD_SIZE(e.r.size); i++) {
				if (i && !(i % 16))
					ccprintf("\n          ");
				ccprintf(" %02x", e.r.payload[i]);
			}
			ccprintf("\n");
			cflush();
			stamp = e.r.timestamp;
		}
		if (rv)
			ccprintf("Warning: Last attempt to dequeue returned "
				 "%d\n",
				 rv);
		return EC_SUCCESS;
	}

	if (argc != 3) {
		ccprintf("type and size of the entry are required\n");
		return EC_ERROR_PARAM_COUNT;
	}

	type = atoi(argv[1]);
	size = atoi(argv[2]);

	if (type >= FLASH_LOG_NO_ENTRY) {
		ccprintf("type must not exceed %d\n", FLASH_LOG_NO_ENTRY - 1);
		return EC_ERROR_PARAM2;
	}

	if (size > MAX_FLASH_LOG_PAYLOAD_SIZE) {
		ccprintf("size must not exceed %d\n",
			 MAX_FLASH_LOG_PAYLOAD_SIZE);
		return EC_ERROR_PARAM3;
	}

	for (i = 0; i < size; i++)
		e.r.payload[i] = type + i;
	flash_log_add_event(type, size, e.r.payload);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flog, command_flash_log,
			"[-e] ][[stamp]|[<type> <size>]]",
			"Dump on the console the flash log contents,"
			"optionally erasing it\n"
			"or add a new entry of <type> and <size> bytes");
#endif
