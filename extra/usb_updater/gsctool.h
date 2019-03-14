/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EXTRA_USB_UPDATER_GSCTOOL_H
#define __EXTRA_USB_UPDATER_GSCTOOL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "usb_if.h"

/*
 * gsctool uses this structure to keep information about the communications
 * channel used to talk to the Cr50, and about the state of the Cr50 image.
 */
struct transfer_descriptor {
	/*
	 * Set to true for use in an upstart script. Do not reboot after
	 * transfer, and do not transfer RW if versions are the same.
	 *
	 * When using in development environment it is beneficial to transfer
	 * RW images with the same version, as they get started based on the
	 * header timestamp.
	 */
	int upstart_mode;
	/*
	 * Override in case updater is used w/ boards that do not follow
	 * the cr50 versioning scheme.
	 */
	int background_update_supported;

	/*
	 * offsets of RO and WR sections available for update (not currently
	 * active).
	 */
	uint32_t ro_offset;
	uint32_t rw_offset;

	/* Do not reset the H1 immediately after update, wait for TPM reset. */
	int post_reset;

	/* Type of channel used to communicate with Cr50. */
	enum transfer_type {
		usb_xfer = 0,  /* usb interface. */
		dev_xfer = 1,  /* /dev/tpm0 */
		ts_xfer = 2    /* trunks_send */
	} ep_type;
	union {
		struct usb_endpoint uep;
		int tpm_fd;
	};
};

/*
 * These are values returned by the gsctool utility, they are interpreted by
 * the startup files to decide how to proceed (try to update to a new Cr50
 * image or not).
 */
enum exit_values {
	noop = 0,	  /* All up to date, no update needed. */
	all_updated = 1,  /* Update completed, reboot required. */
	rw_updated  = 2,  /* RO was not updated, reboot required. */
	update_error = 3  /* Something went wrong. */
};


struct board_id {
	uint32_t type;	    /* Board type */
	uint32_t type_inv;  /* Board type (inverted) */
	uint32_t flags;	    /* Flags */
};

enum board_id_action {
	bid_none,
	bid_get,
	bid_set
};

/*
 * This function allows to retrieve or set (if not initialized) board ID of
 * the H1 chip. If bid_action is bid_get and show_machine_output is set,
 * prints out board ID in a machine-friendly format.
 */
void process_bid(struct transfer_descriptor *td,
		 enum board_id_action bid_action,
		 struct board_id *bid,
		 bool show_machine_output);

/*
 * This function can be used to retrieve the current PP status from Cr50 and
 * prompt the user when a PP press is required.
 *
 * Physical presence can be required by different gsctool options, for which
 * Cr50 behavior also differs. The 'command' and 'poll_type' parameters are
 * used by Cr50 to tell what the host is polling for.
 */
void poll_for_pp(struct transfer_descriptor *td,
		 uint16_t command,
		 uint8_t poll_type);

/*
 * Function used to send vendor command to the Cr50 and receive a response.
 * Returns the error code from TPM response header, which is set to zero on
 * success.
 */
uint32_t send_vendor_command(struct transfer_descriptor *td,
			     uint16_t subcommand,
			     const void *command_body,
			     size_t command_body_size,
			     void *response,
			     size_t *response_size);


#endif // __EXTRA_USB_UPDATER_GSCTOOL_H
