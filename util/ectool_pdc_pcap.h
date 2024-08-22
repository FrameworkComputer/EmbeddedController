/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ECTOOL_PDC_PCAP_H
#define ECTOOL_PDC_PCAP_H

#include <stdio.h>

#include <sys/time.h>

FILE *pdc_pcap_open(const char *file);
int pdc_pcap_append(FILE *fp, struct timeval tv, const void *pl, size_t pl_sz);
int pdc_pcap_close(FILE *fp);

#endif /* ECTOOL_PDC_PCAP_H */
