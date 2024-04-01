/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_LPC_H
#define __CROS_EC_LPC_H

#include "common.h"
#include "host_command.h"

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
 * Send an aux byte to host via keyboard port 0x60.
 *
 * @param chr		Byte to send
 * @param send_irq	If non-zero, asserts IRQ
 */
void lpc_aux_put_char(uint8_t chr, int send_irq);

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
	LPC_HOST_EVENT_ALWAYS_REPORT,
	LPC_HOST_EVENT_COUNT,
};

/**
 * Get current state of host events.
 */
host_event_t lpc_get_host_events(void);

#ifdef TEST_BUILD
/**
 * Set host events.
 */
void lpc_set_host_event_state(host_event_t events);
#endif

/**
 * Get host events that are set based on the type provided.
 *
 * @param type		Event type
 */
host_event_t lpc_get_host_events_by_type(enum lpc_host_event_type type);

/**
 * Set the event mask for the specified event type.
 *
 * @param type		Event type
 * @param mask		New event mask
 */
void lpc_set_host_event_mask(enum lpc_host_event_type type, host_event_t mask);

/**
 * Get host event mask based on the type provided.
 *
 * @param type		Event type
 */
host_event_t lpc_get_host_event_mask(enum lpc_host_event_type type);

/**
 * Clear and return the lowest host event.
 */
int lpc_get_next_host_event(void);

/**
 * Set the EC_LPC_STATUS_* mask for the specified status.
 */
void lpc_set_acpi_status_mask(uint8_t mask);

/**
 * Clear the EC_LPC_STATUS_* mask for the specified status.
 */
void lpc_clear_acpi_status_mask(uint8_t mask);

/**
 * Return the state of platform reset.
 *
 * @return non-zero if PLTRST# is asserted (low); 0 if not asserted (high).
 */
int lpc_get_pltrst_asserted(void);

/* Disable LPC ACPI interrupts */
void lpc_disable_acpi_interrupts(void);

/* Enable LPC ACPI interrupts */
void lpc_enable_acpi_interrupts(void);

/**
 * Update host event status. This function is called whenever host event bits
 * need to be updated based on initialization complete or host event mask
 * update or when a new host event is set or cleared.
 */
void lpc_update_host_event_status(void);

/*
 * This is a weak function defined in host_events_commands.c to override the
 * LPC_HOST_EVENT_ALWAYS_REPORT mask. It can be implemented by boards if there
 * is a need to use custom mask.
 */
host_event_t lpc_override_always_report_mask(void);

/* Initialize LPC masks. */
void lpc_init_mask(void);

/*
 * Clear LPC masks for SMI, SCI and wake upon resume from S3. This is done to
 * mask these events until host unmasks them itself.
 */
void lpc_s3_resume_clear_masks(void);

#endif /* __CROS_EC_LPC_H */
