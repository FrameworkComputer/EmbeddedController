/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_EVENT_LOG_H
#define __CROS_EC_EVENT_LOG_H

#include "common.h"
#include "compile_time_macros.h"
#include "config.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

enum flash_event_type {
	FE_LOG_START = 0,
	FE_LOG_CORRUPTED = 1,
	FE_TPM_I2C_ERROR = 2,
	FE_LOG_OVERFLOWS = 3, /* A single byte, overflow counter. */
	FE_LOG_LOCKS = 4, /* A single byte, lock failures counter. */
	FE_LOG_NVMEM = 5, /* NVMEM failure, variable structure. */
	FE_LOG_TPM_WIPE_ERROR = 6, /* Failed to wipe the TPM */
	FE_LOG_TRNG_STALL = 7, /* Stall while retrieving a random number. */
	FE_LOG_DCRYPTO_FAILURE = 8, /* Dcrypto had to be reset. */

	/*
	 * Fixed padding value makes it easier to parse log space
	 * snapshots.
	 */
	FE_LOG_PAD = 253,
	/* A test event, the highest possible event type value. */
	FE_LOG_TEST = 254,
};
struct flash_log_entry {
	/*
	 * Until real wall clock time is available this is a monotonically
	 * increasing entry number.
	 *
	 * TODO(vbendeb): however unlikely, there could be multiple events
	 *    logged within the same 1 second interval. There needs to be a
	 *    way to handle this. Maybe storing incremental time, having only
	 *    the very first entry in the log carry the real time. Maybe
	 *    enhancing the log traversion function to allow multiple entries
	 *    with the same timestamp value.
	 */
	uint32_t timestamp;
	uint8_t size; /* [7:6] caller-def'd [5:0] payload size in bytes. */
	uint8_t type; /* event type, caller-defined */
	uint8_t crc;
	uint8_t payload[0]; /* optional additional data payload: 0..63 bytes. */
} __packed;

/* Payloads for various log events. */
/* NVMEM failures. */
enum nvmem_failure_type {
	NVMEMF_MALLOC = 0,
	NVMEMF_PH_SIZE_MISMATCH = 1,
	NVMEMF_READ_UNDERRUN = 2,
	NVMEMF_INCONSISTENT_FLASH_CONTENTS = 3,
	NVMEMF_MIGRATION_FAILURE = 4,
	NVMEMF_LEGACY_ERASE_FAILURE = 5,
	NVMEMF_EXCESS_DELETE_OBJECTS = 6,
	NVMEMF_UNEXPECTED_LAST_OBJ = 7,
	NVMEMF_MISSING_OBJECT = 8,
	NVMEMF_SECTION_VERIFY = 9,
	NVMEMF_PRE_ERASE_MISMATCH = 10,
	NVMEMF_PAGE_LIST_OVERFLOW = 11,
	NVMEMF_CIPHER_ERROR = 12,
	NVMEMF_CORRUPTED_INIT = 13,
	NVMEMF_CONTAINER_HASH_MISMATCH = 14,
	NVMEMF_UNRECOVERABLE_INIT = 15,
	NVMEMF_NVMEM_WIPE = 16,
};

/* Not all nvmem failures require payload. */
struct nvmem_failure_payload {
	uint8_t failure_type;
	union {
		uint16_t size; /* How much memory was requested. */
		struct {
			uint16_t ph_offset;
			uint16_t expected;
		} ph __packed;
		uint16_t underrun_size; /* How many bytes short. */
		uint8_t last_obj_type;
	} __packed;
} __packed;

/* Returned in the "type" field, when there is no entry available */
#define FLASH_LOG_NO_ENTRY 0xff
#define MAX_FLASH_LOG_PAYLOAD_SIZE ((1 << 6) - 1)
#define FLASH_LOG_PAYLOAD_SIZE_MASK (MAX_FLASH_LOG_PAYLOAD_SIZE)

#define FLASH_LOG_PAYLOAD_SIZE(size) ((size) & FLASH_LOG_PAYLOAD_SIZE_MASK)
/* Size of log entry for a specific payload size. */
#define FLASH_LOG_ENTRY_SIZE(payload_sz)                                  \
	((FLASH_LOG_PAYLOAD_SIZE(payload_sz) +                            \
	  sizeof(struct flash_log_entry) + CONFIG_FLASH_WRITE_SIZE - 1) & \
	 ~(CONFIG_FLASH_WRITE_SIZE - 1))

/*
 * Flash log implementation expects minimum flash write size not to exceed the
 * log header structure size.
 *
 * It will be easy to extend implementation to cover larger write sizes if
 * necessary.
 */
BUILD_ASSERT(sizeof(struct flash_log_entry) >= CONFIG_FLASH_WRITE_SIZE);

/* A helper structure to represent maximum size flash elog event entry. */
union entry_u {
	uint8_t entry[FLASH_LOG_ENTRY_SIZE(MAX_FLASH_LOG_PAYLOAD_SIZE)];
	struct flash_log_entry r;
};

#define COMPACTION_SPACE_PRESERVE (CONFIG_FLASH_LOG_SPACE / 4)
#define STARTUP_LOG_FULL_WATERMARK (CONFIG_FLASH_LOG_SPACE * 3 / 4)
#define RUN_TIME_LOG_FULL_WATERMARK (CONFIG_FLASH_LOG_SPACE * 9 / 10)

/*
 * Add an entry to the event log. No errors are reported, as there is little
 * we can do if logging attempt fails.
 */
void flash_log_add_event(uint8_t type, uint8_t size, void *payload);

/*
 * Report the next event after the passed in number.
 *
 * Return
 *  - positive integer - the size of the retrieved event
 *  - 0 if there is no more events
 *  - -EC_ERROR_BUSY if event logging is in progress
 *  - -EC_ERROR_MEMORY_ALLOCATION if event body does not fit into the buffer
 *  - -EC_ERROR_INVAL in case log storage is corrupted
 */
int flash_log_dequeue_event(uint32_t event_after, void *buffer,
			    size_t buffer_size);

void flash_log_register_flash_control_callback(
	void (*flash_control)(int enable));

/*
 * Set log timestamp base. The argument is current epoch time in seconds.
 * Return value of EC_ERROR_INVAL indicates attempt to set the timestamp base
 * to a value below the latest log entry timestamp.
 */
enum ec_error_list flash_log_set_tstamp(uint32_t tstamp);

/* Get current log timestamp value. */
uint32_t flash_log_get_tstamp(void);

#if defined(TEST_BUILD)
void flash_log_init(void);
extern uint32_t last_used_timestamp;
extern uint32_t lock_failures_count;
extern uint8_t log_event_in_progress;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_EVENT_LOG_H */
