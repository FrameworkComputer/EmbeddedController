/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FIFO buffer of MKBP events for Chrome EC */

#ifndef __CROS_EC_MKBP_FIFO_H
#define __CROS_EC_MKBP_FIFO_H

#include "common.h"
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIFO_DEPTH 16

/**
 * Update the "soft" FIFO depth (size). The new depth should be less or
 * equal FIFO_DEPTH
 *
 * @param new_max_depth		New FIFO depth.
 */
void mkbp_fifo_depth_update(uint8_t new_max_depth);

/**
 * Clear all keyboard events from the MKBP common FIFO
 */
void mkbp_fifo_clear_keyboard(void);

/**
 * Clear the entire MKBP common FIFO.
 */
void mkbp_clear_fifo(void);

/**
 * Add an element to the common MKBP FIFO.
 *
 * @param event_type	The MKBP event type.
 * @param buffp		Pointer to the event data to enqueue.
 * @return EC_SUCCESS if entry added, EC_ERROR_OVERFLOW if FIFO is full.
 */
int mkbp_fifo_add(uint8_t event_type, const uint8_t *buffp);

/**
 * Remove an element from the common MKBP FIFO.
 *
 * @param out		Pointer to the event data to dequeue.
 * @param event_type	The MKBP event type.
 * @return size of the returned event, EC_ERROR_BUSY if type mismatch.
 */
int mkbp_fifo_get_next_event(uint8_t *out, enum ec_mkbp_event evt);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MKBP_FIFO_H */
