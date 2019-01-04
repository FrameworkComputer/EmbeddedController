/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_CLOSED_SOURCE_SET1_H
#define __EC_BOARD_CR50_CLOSED_SOURCE_SET1_H


/**
 * Configure the GPIOs specific to the BOARD_CLOSED_SOURCE_SET1 board strapping
 * option.  This includes the FACTORY_MODE, CHROME_SEL, and EXIT_FACTORY_MODE
 * signals.
 */
void closed_source_set1_configure_gpios(void);

/**
 * Drive the GPIOs specific to BOARD_CLOSED_SOURCE_SET1 to match the current
 * factory mode setting.
 */
void closed_source_set1_update_factory_mode(void);

/**
 * In response to a TPM_MODE disable, drive the GPIOs specific to
 * BOARD_CLOSED_SOURCE_SET1 to match the diagnostic state setting.
 */
void close_source_set1_disable_tpm(void);


#endif   /* ! __EC_BOARD_CR50_CLOSED_SOURCE_SET1_H */
