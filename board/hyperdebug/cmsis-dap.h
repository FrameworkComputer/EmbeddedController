#ifndef _HYPERDEBUG_BOARD_UTIL__H_
#define _HYPERDEBUG_BOARD_UTIL__H_

#include "queue.h"

extern struct queue const cmsis_dap_tx_queue;
extern struct queue const cmsis_dap_rx_queue;

extern uint8_t rx_buffer[256];
extern uint8_t tx_buffer[256];

/*
 * Declaration of handlers of CMSIS-DAP vendor extension commands, implemented
 * in other files besides cmsis-dap.c .
 */
void dap_goog_i2c(size_t peek_c);
void dap_goog_i2c_device(size_t peek_c);
void dap_goog_gpio(size_t peek_c);

#endif
