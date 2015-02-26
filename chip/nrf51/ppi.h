/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * PPI channels are a way to connect NRF51 EVENTs to TASKs without software
 * involvement. They are like SHORTs, except between peripherals.
 *
 * PPI groups are user-defined sets of channels that can be enabled and disabled
 * together.
 */

/*
 * Reserve a pre-programmed PPI channel.
 *
 * Return EC_SUCCESS if ppi_chan is a pre-programmed channel that was not in
 * use, otherwise returns EC_ERROR_BUSY.
 */
int ppi_request_pre_programmed_channel(int ppi_chan);

/*
 * Reserve an available PPI channel.
 *
 * Return EC_SUCCESS and set the value of ppi_chan to an available PPI
 * channel.  If no channel is available, return EC_ERROR_BUSY.
 */
int ppi_request_channel(int *ppi_chan);

/* Release a PPI channel which was reserved with ppi_request_*_channel.  */
void ppi_release_channel(int ppi_chan);

/*
 * Reserve a PPI group.
 *
 * Return EC_SUCCESS and set the value of ppi_group to an available PPI
 * group.  If no group is available, return EC_ERROR_BUSY.
 */
int ppi_request_group(int *ppi_group);

/* Release a PPI channel which was reserved with ppi_request_*_channel.  */
void ppi_release_group(int ppi_group);
