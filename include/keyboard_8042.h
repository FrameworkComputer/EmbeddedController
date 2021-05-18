/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_EC_KEYBOARD_8042_H
#define __CROS_EC_KEYBOARD_8042_H

#include "common.h"
#include "button.h"
/**
 * Called by power button handler and button interrupt handler.
 *
 * This function sends the corresponding make or break code to the host.
 */
void button_state_changed(enum keyboard_button_type button, int is_pressed);

/**
 * Notify the keyboard module when a byte is written by the host.
 *
 * Note: This is called in interrupt context by the LPC interrupt handler.
 *
 * @param data		Byte written by host
 * @param is_cmd        Is byte command (!=0) or data (0)
 */
void keyboard_host_write(int data, int is_cmd);

/**
 * Get the amount of free 8042 buffer slots
 * this is used to put backpressure on the host
 * if the keyboard task gets too slow
 * @return bytes_avaliable
 */
int keyboard_host_write_avaliable(void);

/*
 * Board specific callback function when a key state is changed.
 *
 * A board may watch key events and create some easter eggs, or apply dynamic
 * translation to the make code (i.e., remap keys).
 *
 * Returning EC_SUCCESS implies *make_code is still a valid make code to be
 * processed. Any other return value will abort processing of this make code.
 * If callback alters *make_code or aborts key processing when pressed=1, it is
 * responsible for also altering/aborting the matching pressed=0 call.
 *
 * @param make_code	Pointer to scan code (set 2) of key in action.
 * @param pressed	Is the key being pressed (1) or released (0).
 */
enum ec_error_list keyboard_scancode_callback(uint16_t *make_code,
					      int8_t pressed);

/**
 * Send aux data to host from interrupt context.
 *
 * @param data	Aux response to send to host.
 */
void send_aux_data_to_host_interrupt(uint8_t data);

/**
 * Returns how many bytes of aux data are available in the queue
 */
int aux_buffer_available(void);

/**
 * Send aux data to device.
 *
 * @param data	Aux data to send to device.
 */
void send_aux_data_to_device(uint8_t data);

/*
 * This function can help change the keyboard top row layout as presented to the
 * AP. If changing the position of the "Refresh" key from T3, you may also need
 * to change KEYBOARD_ROW_REFRESH accordingly so that recovery mode can work on
 * the EC side of things (also see related CONFIG_KEYBOARD_REFRESH_ROW3)
 */
__override_proto
const struct ec_response_keybd_config *board_vivaldi_keybd_config(void);

#endif  /* __CROS_EC_KEYBOARD_8042_H */
