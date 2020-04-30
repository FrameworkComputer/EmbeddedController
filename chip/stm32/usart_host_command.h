/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USART_HOST_COMMAND_H
#define __CROS_EC_USART_HOST_COMMAND_H

#include <stdarg.h>  /* For va_list */
#include "common.h"
#include "gpio.h"
#include "host_command.h"
#include "usart.h"

/*
 * Add data to host command layer buffer.
 */
size_t usart_host_command_rx_append_data(struct usart_config const *config,
					 const uint8_t *src, size_t count);

/*
 * Remove data from the host command layer buffer.
 */
size_t usart_host_command_tx_remove_data(struct usart_config const *config,
					 uint8_t *dest);

/*
 * Get USART protocol information. This function is called in runtime if
 * board's host command transport is USART.
 */
enum ec_status usart_get_protocol_info(struct host_cmd_handler_args *args);

/*
 * Initialize USART host command layer.
 */
void usart_host_command_init(void);

#endif /* __CROS_EC_USART_HOST_COMMAND_H */
