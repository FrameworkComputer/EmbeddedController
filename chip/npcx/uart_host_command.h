/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UART_HOST_COMMAND_H
#define __CROS_EC_UART_HOST_COMMAND_H

/*
 * Initialize UART host command layer.
 */
void uart_host_command_init(void);
/*
 * Get UART protocol information. This function is called in runtime if
 * board's host command transport is UART.
 */
enum ec_status uart_get_protocol_info(struct host_cmd_handler_args *args);

#endif /* __CROS_EC_UART_HOST_COMMAND_H */
