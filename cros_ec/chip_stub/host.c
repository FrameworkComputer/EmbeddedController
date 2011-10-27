/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * host.c -- implements the LPC driver of EC lib and provides simulation
 *           functions.
 */

#include <string.h>  /* FIXME: remove after we have our-own mem*(). */
#include "cros_ec/include/ec_common.h"
#include "cros_ec/chip_stub/include/keyboard.h"
#include "host_interface/ec_command.h"
#include "host_interface/i8042.h"


static EcAcpiCallback acpi_callback;
static EcI8042Callback i8042_callback;
#define SCAN_CODE_QUEUE_LEN 16
static int scan_code_queue_len = 0;
static uint8_t scan_code_queue[SCAN_CODE_QUEUE_LEN];


EcError EcAcpiRegisterCallback(EcAcpiCallback callback) {
  acpi_callback = callback;

  return EC_SUCCESS;
}


EcError EcI8042RegisterCallback(EcI8042Callback callback) {
  i8042_callback = callback;

  return EC_SUCCESS;
}


EcError EcI8042SendScanCode(int len, uint8_t *scan_code) {
  if ((scan_code_queue_len + len) > SCAN_CODE_QUEUE_LEN) {
    return EC_ERROR_BUFFER_FULL;
  }

  memcpy(&scan_code_queue[scan_code_queue_len], scan_code, len);
  scan_code_queue_len += len;

  return EC_SUCCESS;
}



/************* Simulation functions ***************/
int SimulateAcpiCommand(
    uint8_t command,
    uint8_t data,
    uint8_t *mailbox,
    uint8_t *output) {

  EC_ASSERT(acpi_callback);

  return acpi_callback(command, data, mailbox, output);
}

int SimulateI8042Command(
    uint8_t command,
    uint8_t data,
    uint8_t *output) {

  EC_ASSERT(i8042_callback);

  return i8042_callback(command, data, output);
}

EcError PullI8042ScanCode(uint8_t *buf) {
  EC_ASSERT(buf);

  if (scan_code_queue_len <= 0) {
    return EC_ERROR_BUFFER_EMPTY;
  }

  *buf = scan_code_queue[0];
  memmove(&scan_code_queue[0], &scan_code_queue[1], --scan_code_queue_len);

  return EC_SUCCESS;
}
