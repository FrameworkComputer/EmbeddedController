/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CHIP_G_I2CS_H
#define __CHIP_G_I2CS_H

#include <stddef.h>

/*
 * Write complete interrupt callback function prototype. This function expects
 * two parameters: the address of the buffer containing received data and
 * number of bytes in the buffer.
 */
typedef void (*wr_complete_handler_f)(void *i2cs_data, size_t i2cs_data_size);

/* Register the write complete interrupt handler. */
int i2cs_register_write_complete_handler(wr_complete_handler_f wc_handler);

/*
 * Post a byte for the master to read. Blend the byte into the appropriate
 * 4byte register of the master read register file.
 */
void i2cs_post_read_data(uint8_t byte_to_read);

#endif /* ! __CHIP_G_I2CS_H */
