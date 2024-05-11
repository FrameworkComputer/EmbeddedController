/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC-EC communication, functions and definition for server.
 */

#ifndef EC_EC_COMM_SERVER_H_
#define EC_EC_COMM_SERVER_H_

#include "consumer.h"
#include "queue.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct queue const ec_ec_comm_server_input;
extern struct queue const ec_ec_comm_server_output;

void ec_ec_comm_server_written(struct consumer const *consumer, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* EC_EC_COMM_SERVER_H_ */
