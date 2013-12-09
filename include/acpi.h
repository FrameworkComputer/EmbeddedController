/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ACPI EC interface block. */

#ifndef __CROS_EC_ACPI_H
#define __CROS_EC_ACPI_H

#include <stdint.h>

/**
 * Handle AP write to EC via the ACPI I/O port.
 *
 * @param is_cmd	Is write command (is_cmd=1) or data (is_cmd=0)
 * @param value         Value written to cmd or data register by AP
 * @param result        Value for AP to read from data port, if any
 * @return              True if *result was updated by this call
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *result);

#endif	/* __CROS_EC_ACPI_H */
