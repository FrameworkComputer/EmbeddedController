/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Physical presence detection
 */
#ifndef __CROS_EC_PHYSICAL_PRESENCE_H
#define __CROS_EC_PHYSICAL_PRESENCE_H

/**
 * Start physical presence detect.
 *
 * If the physical presence sequence is successful, callback() will be called
 * from the hook task context as a deferred function.
 *
 * On failure or abort, callback() will not be called.
 *
 * @param is_long	Use long (!=0) or short (0) sequence)
 * @param callback	Function to call when successful
 * @return EC_SUCCESS, EC_BUSY if detect already in progress, or other
 *	   non-zero error code if error.
 */
int physical_detect_start(int is_long, void (*callback)(void));

/**
 * Check if a physical detect attempt is in progress
 *
 * @return non-zero if in progress
 */
int physical_detect_busy(void);

/**
 * Abort a currently-running physical presence detect.
 *
 * Note there is a race condition between stopping detect and a running
 * detect finishing and calling its callback.  The intent of this function
 * is not to prevent that, but instead to avoid an aborted physical detect
 * tying up the button for long periods when we no longer care.
 */
void physical_detect_abort(void);

/**
 * Handle a physical detect button press.
 *
 * This may be called from interrupt level.
 *
 * Returns EC_SUCCESS if the press was consumed, or EC_ERROR_NOT_HANDLED if
 * physical detect was idle (so the press is for someone else).
 */
int physical_detect_press(void);

/**
 * Start/stop capturing the button for physical presence.
 *
 * When enabled, a debounced button press+release should call
 * physical_detect_press().
 *
 * This should be implemented by the board.
 *
 * @param enable	Enable (!=0) or disable (==0) capturing button.
 */
void board_physical_presence_enable(int enable);

/**
 * An API to report physical presence FSM state to an external entity. Of
 * interest are states when key press is currently required or is expected
 * soon.
 */
enum pp_fsm_state {
	PP_OTHER = 0,
	PP_AWAITING_PRESS = 1,
	PP_BETWEEN_PRESSES = 2,
};
enum pp_fsm_state physical_presense_fsm_state(void);

#endif /* __CROS_EC_PHYSICAL_PRESENCE_H */
