/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB API definitions.
 *
 * This file includes definitions needed by common code that wants to control
 * the state of the USB peripheral, but doesn't need to know about the specific
 * implementation.
 */

#ifndef __CROS_EC_USB_API_H
#define __CROS_EC_USB_API_H

/*
 * Initialize the USB peripheral, enabling its clock and configuring the DP/DN
 * GPIOs correctly.  This function is called via an init hook (unless the board
 * defined CONFIG_USB_INHIBIT_INIT), but may need to be called again if
 * usb_release is called.  This function will call usb_connect by default
 * unless CONFIG_USB_INHIBIT_CONNECT is defined.
 */
void usb_init(void);

/* Check if USB peripheral is enabled. */
int usb_is_enabled(void);

/*
 * Enable the pullup on the DP line to signal that this device exists to the
 * host and to start the enumeration process.
 */
void usb_connect(void);

/*
 * Disable the pullup on the DP line.  This causes the device to be disconnected
 * from the host.
 */
void usb_disconnect(void);

/*
 * Disconnect from the host by calling usb_disconnect and then turn off the USB
 * peripheral, releasing its GPIOs and disabling its clock.
 */
void usb_release(void);

/*
 * Returns true if USB device is currently suspended.
 * Requires CONFIG_USB_SUSPEND to be defined.
 */
int usb_is_suspended(void);

/*
 * Returns true if USB remote wakeup is currently enabled by host.
 * Requires CONFIG_USB_SUSPEND to be defined, always return 0 if
 * CONFIG_USB_REMOTE_WAKEUP is not defined.
 */
int usb_is_remote_wakeup_enabled(void);

/*
 * Preserve in non-volatile memory the state of the USB hardware registers
 * which cannot be simply re-initialized when powered up again.
 */
void usb_save_suspended_state(void);

/*
 * Restore from non-volatile memory the state of the USB hardware registers
 * which was lost by powering them down.
 */
void usb_restore_suspended_state(void);

/*
 * Tell the host to wake up. Does nothing if CONFIG_USB_REMOTE_WAKEUP is not
 * defined.
 *
 * Returns immediately, suspend status can be checked using usb_is_suspended.
 */
#ifdef CONFIG_USB_REMOTE_WAKEUP
void usb_wake(void);
#else
static inline void usb_wake(void) {}
#endif

/* Board-specific USB wake, for side-band wake, called by usb_wake above. */
void board_usb_wake(void);

#endif /* __CROS_EC_USB_API_H */
