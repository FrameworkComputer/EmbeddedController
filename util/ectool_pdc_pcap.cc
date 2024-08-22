/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ectool_pdc_pcap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct pcap_hdr_s {
	uint32_t magic_number; /* magic number */
	uint16_t version_major; /* major version number */
	uint16_t version_minor; /* minor version number */
	int32_t thiszone; /* GMT to local correction */
	uint32_t sigfigs; /* accuracy of timestamps */
	uint32_t snaplen; /* max length of captured packets, in octets */
	uint32_t network; /* data link type */
};

struct pcaprec_hdr_s {
	uint32_t ts_sec; /* timestamp seconds */
	uint32_t ts_usec; /* timestamp microseconds */
	uint32_t incl_len; /* number of octets of packet saved in file */
	uint32_t orig_len; /* actual length of packet */
};

FILE *pdc_pcap_open(const char *pcap_file)
{
	FILE *fp;
	struct pcap_hdr_s hdr;

	fp = fopen(pcap_file, "w");
	if (fp == NULL) {
		fprintf(stderr, "Could not open pcap file %s\n", pcap_file);
		return NULL;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic_number = 0xa1b2c3d4; /* timestamp in microseconds */
	hdr.version_major = 2;
	hdr.version_minor = 4;
	hdr.snaplen = 512;
	hdr.network = 147; /* DLT_USER0 */

	fwrite(&hdr, sizeof(hdr), 1, fp);

	return fp;
}

int pdc_pcap_append(FILE *fp, struct timeval tv, const void *pl, size_t pl_sz)
{
	struct pcaprec_hdr_s pkt;

	pkt.ts_sec = tv.tv_sec;
	pkt.ts_usec = tv.tv_usec;

	pkt.incl_len = pl_sz;
	pkt.orig_len = pl_sz;

	fwrite(&pkt, sizeof(pkt), 1, fp);
	fwrite(pl, pl_sz, 1, fp);

	return 0;
}

int pdc_pcap_close(FILE *fp)
{
	if (fp != NULL)
		return fclose(fp);

	return 0;
}
