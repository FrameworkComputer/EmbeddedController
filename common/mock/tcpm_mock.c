/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for the TCPM interface */

#include "common.h"
#include "console.h"
#include "memory.h"
#include "mock/tcpm_mock.h"

struct mock_tcpm_t mock_tcpm[CONFIG_USB_PD_PORT_MAX_COUNT];

/**
 * Gets the next waiting RX message.
 *
 * @param port Type-C port number
 * @param payload Pointer to location to copy payload of PD message
 * @param header The header of PD message
 *
 * @return EC_SUCCESS or error
 */
int tcpm_dequeue_message(int port, uint32_t *payload, int *header)
{
	if (!tcpm_has_pending_message(port))
		return EC_ERROR_BUSY;

	*header = mock_tcpm[port].mock_header;
	memcpy(payload, mock_tcpm[port].mock_rx_chk_buf,
		sizeof(mock_tcpm[port].mock_rx_chk_buf));

	return EC_SUCCESS;
}

/**
 * Returns true if the tcpm has RX messages waiting to be consumed.
 */
int tcpm_has_pending_message(int port)
{
	return mock_tcpm[port].mock_has_pending_message;
}

/**
 * Resets all mock TCPM ports
 */
void mock_tcpm_reset(void)
{
	int port;

	for (port = 0 ; port < CONFIG_USB_PD_PORT_MAX_COUNT ; ++port)
		mock_tcpm[port].mock_has_pending_message = 0;
}

/**
 * Sets up a message to be received, with optional data payload. If cnt==0,
 * then data can be NULL.
 */
void mock_tcpm_rx_msg(int port, uint16_t header, int cnt, const uint32_t *data)
{
	mock_tcpm[port].mock_header = header;
	if (cnt > 0) {
		int idx;

		for (idx = 0 ; (idx < cnt) && (idx < MOCK_CHK_BUF_SIZE) ; ++idx)
			mock_tcpm[port].mock_rx_chk_buf[idx] = data[idx];
	}
	mock_tcpm[port].mock_has_pending_message = 1;
}
