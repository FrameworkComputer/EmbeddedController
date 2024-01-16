/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FAILURE_RESPONSE_H
#define FAILURE_RESPONSE_H
/* Failure Response Status as defined in
 * Crisis_Mode-cmdSequence_v2.pdf
 */
#define CRC_FAILURE (1 << 0)
#define ILLEGAL_PAYLOAD_LENGTH (1 << 1)
#define ILLEGAL_HEADER_OFFSET (1 << 2)
#define ILLEGAL_HEADER_FORMAT (1 << 3)
#define ILLEGAL_FW_IMAGE_OFFSET (1 << 2)
#define SPI_FLASH_ACCESS_ERROR (1 << 3)
#define ILLEGAL_FLASH_LENGTH (1 << 4)

#define FAILURE_RESP_STATUS_BIT (1 << 7)

#define RESP_CMD_POS 0
#define RESP_FAILURE_STATUS_POS 1

#endif /* #ifndef FAILURE_RESPONSE_H */
