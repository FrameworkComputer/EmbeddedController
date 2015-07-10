/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "sps.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Function to test SPS driver. It expects the host to send SPI frames of size
 * <size> (not exceeding 1100) of the following format:
 *
 * <size/256> <size%256> [<size> bytes of payload]
 *
 * Once the frame is received, it is sent back. The host can receive it and
 * compare with the original.
 */

 /*
  * Receive callback implemets a simple state machine, it could be in one of
  * three states:  not started, receiving frame, frame finished.
  */

enum sps_test_rx_state {
	spstrx_not_started,
	spstrx_receiving,
	spstrx_finished
};

static enum sps_test_rx_state rx_state;
/* Storage for the received frame. Size chosen arbitrarily to match the
 * external test code. */
static uint8_t test_frame[1100];

/*
 * To verify different alignment cases, the frame is saved in the buffer
 * starting with a certain offset (in range 0..3).
 */
static size_t frame_base;
/*
 * This is the index of the next location where received data will be added
 * to. Points to the end of the received frame once it has been pulled in.
 */
static size_t frame_index;

static void sps_receive_callback(uint8_t *data, size_t data_size,
				 int cs_enabled)
{
	static size_t frame_size; /* Total size of the frame being received. */
	size_t to_go; /* Number of bytes still to receive. */

	if (rx_state == spstrx_not_started) {
		if (data_size < 2)
			return; /* Something went wrong.*/

		frame_size = data[0] * 256 + data[1] + 2;
		frame_base = (frame_base + 1) % 3;
		frame_index = frame_base;

		if ((frame_index + frame_size) <= sizeof(test_frame))
			/* Enter 'receiving frame' state. */
			rx_state = spstrx_receiving;
		else
			/*
			 * If we won't be able to receve this much, enter the
			 * 'frame finished' state.
			 */
			rx_state = spstrx_finished;
	}

	if (rx_state == spstrx_finished) {
		/*
		 * If CS was deasserted, prepare to start receiving the next
		 * frame.
		 */
		if (!cs_enabled)
			rx_state = spstrx_not_started;
		return;
	}

	if (frame_size > data_size)
		to_go = data_size;
	else
		to_go = frame_size;

	memcpy(test_frame + frame_index, data, to_go);
	frame_index += to_go;
	frame_size -= to_go;

	if (!frame_size)
		rx_state = spstrx_finished; /* Frame finished.*/
}

static int command_sps(int argc, char **argv)
{
	int count = 0;
	int target = 10; /* Expect 10 frames by default.*/
	char *e;

	rx_state = spstrx_not_started;

	sps_register_rx_handler(SPI_CLOCK_MODE0, SPS_GENERIC_MODE,
				sps_receive_callback);

	if (argc > 1) {
		target = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	/* reset statistic counters */
	sps_rx_count = sps_tx_count = 0;
	sps_tx_empty_count = sps_max_rx_batch = 0;

	while (count++ < target) {
		size_t transmitted;
		size_t to_go;
		size_t index;

		/* Wait for a frame to be received.*/
		while (rx_state != spstrx_finished) {
			watchdog_reload();
			usleep(10);
		}

		/* Transmit the frame back to the host.*/
		index = frame_base;
		to_go = frame_index - frame_base;
		do {
			if ((index == frame_base) && (to_go > 8)) {
				/*
				 * This is the first transmit attempt for this
				 * frame. Send a little just to prime the
				 * transmit FIFO.
				 */
				transmitted = sps_transmit(
					test_frame + index, 8);
			} else {
				transmitted = sps_transmit(
					test_frame + index, to_go);
			}
			index += transmitted;
			to_go -= transmitted;
		} while (to_go);

		/*
		 * Wait for receive state machine to transition out of 'frame
		 * finised' state.
		 */
		while (rx_state == spstrx_finished) {
			watchdog_reload();
			usleep(10);
		}
	}

	sps_unregister_rx_handler();

	ccprintf("Processed %d frames\n", count - 1);
	ccprintf("rx count %d, tx count %d, tx_empty %d, max rx batch %d\n",
		 sps_rx_count, sps_tx_count,
		 sps_tx_empty_count, sps_max_rx_batch);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(spstest, command_sps,
			"<num of frames>",
			"Loop back frames (10 by default) back to the host",
			NULL);
