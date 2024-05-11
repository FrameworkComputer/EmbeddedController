/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_EVENT_LOG_H
#define __CROS_EC_EVENT_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct event_log_entry {
	uint32_t timestamp; /* relative timestamp in milliseconds */
	uint8_t type; /* event type, caller-defined */
	uint8_t size; /* [7:5] caller-def'd [4:0] payload size in bytes */
	uint16_t data; /* type-defined data payload */
	uint8_t payload[0]; /* optional additional data payload: 0..16 bytes */
} __packed;

#define EVENT_LOG_SIZE_MASK 0x1f
#define EVENT_LOG_SIZE(size) ((size) & EVENT_LOG_SIZE_MASK)

/* The timestamp is the microsecond counter shifted to get about a ms. */
#define EVENT_LOG_TIMESTAMP_SHIFT 10 /* 1 LSB = 1024us */
/* Returned in the "type" field, when there is no entry available */
#define EVENT_LOG_NO_ENTRY 0xff

/* Add an entry to the event log. */
void log_add_event(uint8_t type, uint8_t size, uint16_t data, void *payload,
		   uint32_t timestamp);

/*
 * Remove and return an entry from the event log, if available.
 * Returns size of log entry *r.
 */
int log_dequeue_event(struct event_log_entry *r);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_EVENT_LOG_H */
