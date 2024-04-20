#ifndef _HYPERDEBUG_BOARD_UTIL__H_
#define _HYPERDEBUG_BOARD_UTIL__H_

#include "queue.h"

extern struct queue const cmsis_dap_tx_queue;
extern struct queue const cmsis_dap_rx_queue;

extern uint8_t rx_buffer[256];
extern uint8_t tx_buffer[256];

/*
 * If this function returns true, it means that the currently executing handler
 * method must abort and return as soon as possible.
 */
bool cmsis_dap_unwind_requested(void);

/*
 * Routines to be used in the CMSIS-DAP task to add/remove a possibly large
 * number of bytes from the USB queues.  These functions can block waiting for
 * the host computer, and will not return until the given number of bytes has
 * been transferred, except if cmsis_dap_unwind_requested() returns true.
 */
void queue_blocking_add(struct queue const *q, const void *src, size_t count);
void queue_blocking_remove(struct queue const *q, void *dest, size_t count);

/*
 * Declaration of handlers of CMSIS-DAP vendor extension commands, implemented
 * in other files besides cmsis-dap.c .
 */
void dap_goog_i2c(size_t peek_c);
void dap_goog_i2c_device(size_t peek_c);
void dap_goog_gpio(size_t peek_c);

#endif
