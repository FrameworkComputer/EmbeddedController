/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BRYA_CBI_EC_FW_CONFIG_H_
#define __BRYA_CBI_EC_FW_CONFIG_H_

#include "stdint.h"

/****************************************************************************
 * CBI FW_CONFIG layout shared by all Brya boards
 *
 * Source of truth is the program/brya/program.star configuration file.
 */

/*
 * TODO(b/180434685): are these right?
 *	also, remove DB_USB_ABSENT2 after all existing boards have been
 *	set up correctly.
 */

enum ec_cfg_usb_db_type {
	DB_USB_ABSENT = 0,
	DB_USB3_PS8815 = 1,
	DB_USB_ABSENT2 = 15
};

union brya_cbi_fw_config {
	struct {
		/*
		 * TODO(b/180434685): 4 bits?
		 */
		enum ec_cfg_usb_db_type	usb_db : 4;
		uint32_t		reserved_1 : 28;
	};
	uint32_t raw_value;
};

/*
 * Each Brya board must define the default FW_CONFIG options to use
 * if the CBI data has not been initialized.
 */
extern const union brya_cbi_fw_config fw_config_defaults;

/**
 * Initialize the FW_CONFIG from CBI data. If the CBI data is not valid, set the
 * FW_CONFIG to the board specific defaults.
 */
void init_fw_config(void);

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union brya_cbi_fw_config get_fw_config(void);

/**
 * Get the USB daughter board type from FW_CONFIG.
 *
 * @return the USB daughter board type.
 */
enum ec_cfg_usb_db_type ec_cfg_usb_db_type(void);

#endif /* __BRYA_CBI_EC_FW_CONFIG_H_ */
