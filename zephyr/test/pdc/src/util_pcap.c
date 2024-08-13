/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "timer.h"
#include "util_pcap.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys_clock.h>

LOG_MODULE_REGISTER(pdc_util_pcap, LOG_LEVEL_INF);

/*
 * Minimal pcap file structures originally from pcap/pcap.h
 */

struct pcap_hdr {
	uint32_t magic_number; /* magic number */
	uint16_t version_major; /* major version number */
	uint16_t version_minor; /* minor version number */
	int32_t thiszone; /* not used - SHOULD be filled with 0 */
	uint32_t sigfigs; /* not used - SHOULD be filled with 0 */
	uint32_t snaplen; /* max length saved portion of each pkt */
	uint32_t linktype; /* data link type (LINKTYPE_*) */
};

struct pcap_pkthdr {
	uint32_t ts_sec; /* time stamp seconds */
	uint32_t ts_usec; /* time stamp microseconds */
	uint32_t caplen; /* length of portion present in data */
	uint32_t len; /* length of this packet prior to any slicing */
};

FILE *pcap_open(void)
{
	/*
	 * Each test runs in its own dedicated output directory, so
	 * "rts.pcap" will be created as a sibling to "build.log" and
	 * "handler.log".
	 */
	static const char pcap_file[] = "rts.pcap";
	FILE *fp;
	struct pcap_hdr hdr;

	fp = fopen(pcap_file, "w");
	if (fp == NULL) {
		LOG_ERR("Could not open pcap file %s\n", pcap_file);
		return NULL;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic_number = 0xa1b2c3d4; /* timestamp in microseconds */
	hdr.version_major = 2;
	hdr.version_minor = 4;
	hdr.snaplen = 512;
	hdr.linktype = 147; /* DLT_USER0 */

	fwrite(&hdr, sizeof(hdr), 1, fp);

	return fp;
}

void pcap_append(FILE *fp, const void *pl, size_t pl_sz)
{
	struct pcap_pkthdr pkt;
	uint64_t usec = k_ticks_to_us_near32(k_uptime_ticks());

	pkt.ts_sec = usec / USEC_PER_SEC;
	pkt.ts_usec = usec % USEC_PER_SEC;

	pkt.caplen = pl_sz;
	pkt.len = pl_sz;

	fwrite(&pkt, sizeof(pkt), 1, fp);
	fwrite(pl, pl_sz, 1, fp);
}
