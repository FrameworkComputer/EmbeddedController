/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Master/slave identification */

#ifndef __BOARD_KEYBORG_MASTER_SLAVE_H
#define __BOARD_KEYBORG_MASTER_SLAVE_H

/**
 * Get the identity of this chip.
 *
 * @return		1 if master; otherwise, 0.
 */
int master_slave_is_master(void);

/**
 * Synchronize with the other chip. The other chip must also call this
 * function to synchronize.
 *
 * @param timeout_ms	Timeout value in millisecond. If the other chip
 *			doesn't synchronize within this time, the sync
 *			call fails.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
#define master_slave_sync(timeout_ms) \
	master_slave_sync_impl(__FILE__, __LINE__, timeout_ms)
int master_slave_sync_impl(const char *filename, int line, int timeout_ms);

/**
 * Enable/disable master-slave interrupt. Master-slave interrupt is
 * implemented using SYNC1/SYNC2 signal, so this is assuming the master
 * and the slave are in sync waiting for interrupt.
 */
void master_slave_enable_interrupt(void);
void master_slave_disable_interrupt(void);

/* Interrupt the other chip with a 1-ms pulse. */
void master_slave_wake_other(void);

/**
 * Identify this chip and shake hands with the other chip.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int master_slave_init(void);

#endif /* __BOARD_KEYBORG_MASTER_SLAVE_H */
