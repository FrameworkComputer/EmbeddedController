/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Generic API for handling opaque blobs of data. */

#ifndef __CROS_EC_BLOB_H
#define __CROS_EC_BLOB_H

#include <stddef.h>
#include <stdint.h>

/* Call this to send data to the blob-handler */
size_t put_bytes_to_blob(uint8_t *buffer, size_t count);

/* Call this to get data back fom the blob-handler */
size_t get_bytes_from_blob(uint8_t *buffer, size_t count);

/* Implement this to be notified when the blob-handler can take more data */
void blob_is_ready_for_more_bytes(void);

/* Implement this to be notified when the blob-handler has data to give us */
void blob_is_ready_to_emit_bytes(void);

#endif  /* __CROS_EC_BLOB_H */
