/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#ifndef __CROS_EC_LPC_H
#define __CROS_EC_LPC_H

#include "common.h"

/**
 * Return a pointer to the memory-mapped buffer.
 *
 * This buffer is writable at any time, and the host can read it at any time.
 */
uint8_t *lpc_get_memmap_range(void);

/**
 * Return true if keyboard data is waiting for the host to read (TOH is still
 * set).
 */
int lpc_keyboard_has_char(void);

/* Return true if the FRMH is still set */
int lpc_keyboard_input_pending(void);

/**
 * Send a byte to host via keyboard port 0x60.
 *
 * @param chr		Byte to send
 * @param send_irq	If non-zero, asserts IRQ
 */
void lpc_keyboard_put_char(uint8_t chr, int send_irq);

/**
 * Clear the keyboard buffer.
 */
void lpc_keyboard_clear_buffer(void);

/**
 * Send an IRQ to host if there is a byte in buffer already.
 */
void lpc_keyboard_resume_irq(void);

/**
 * Return non-zero if the COMx interface has received a character.
 */
int lpc_comx_has_char(void);

/**
 * Return the next character pending on the COMx interface.
 */
int lpc_comx_get_char(void);

/**
 * Put a character to the COMx LPC interface.
 */
void lpc_comx_put_char(int c);

/*
 * Low-level LPC interface for host events.
 *
 * For use by host_event_commands.c.  Other modules should use the methods
 * provided in host_command.h.
 */

/* Types of host events */
enum lpc_host_event_type {
	LPC_HOST_EVENT_SMI = 0,
	LPC_HOST_EVENT_SCI,
	LPC_HOST_EVENT_WAKE,
};

/**
 * Set the event state.
 */
void lpc_set_host_event_state(uint32_t mask);

/**
 * Clear and return the lowest host event.
 */
int lpc_query_host_event_state(void);

/**
 * Set the event mask for the specified event type.
 *
 * @param type		Event type
 * @param mask		New event mask
 */
void lpc_set_host_event_mask(enum lpc_host_event_type type, uint32_t mask);

/**
 * Return the event mask for the specified event type.
 */
uint32_t lpc_get_host_event_mask(enum lpc_host_event_type type);

/**
 * Return the state of platform reset.
 *
 * @return non-zero if PLTRST# is asserted (low); 0 if not asserted (high).
 */
int lpc_get_pltrst_asserted(void);

#endif  /* __CROS_EC_LPC_H */
