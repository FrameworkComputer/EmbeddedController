/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Select DMA stream-channel mapping
 *
 * This selects which stream (peripheral) to be used on a specific channel.
 * Some STM32 chips simply logically OR requests, thus do not require this
 * selection.
 *
 * @param channel: (Global) channel # base 0 (Note some STM32s use base 1)
 * @param peripheral: Refer to the TRM for 'peripheral request signals'
 */
void dma_select_channel(enum dma_channel channel, unsigned char stream);
