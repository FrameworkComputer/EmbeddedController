/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_SN_BITS_H
#define __EC_CHIP_G_SN_BITS_H

#include "board_space.h"

/**
 * Reads the SN data from the flash INFO1 space.
 *
 * @param id    Pointer to a sn_data structure to fill
 *
 * @return      EC_SUCCESS or an error code in case of failure.
 */
int read_sn_data(struct sn_data *sn);

#endif  /* ! __EC_CHIP_G_SN_BITS_H */
