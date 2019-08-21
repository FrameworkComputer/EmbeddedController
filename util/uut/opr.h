/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file defines the UART console application operations. */

#ifndef __UTIL_UUT_OPR_H
#define __UTIL_UUT_OPR_H

/*---------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */

/* Baud rate scan steps: */
#define BR_BIG_STEP 20       /* in percents from current baud rate */
#define BR_MEDIUM_STEP 10    /* in percents from current baud rate */
#define BR_SMALL_STEP 1      /* in percents from current baud rate */
#define BR_MIN_STEP 5        /* in absolute baud rate units */
#define BR_LOW_LIMIT 400     /* Automatic BR detection starts at this value */
#define BR_HIGH_LIMIT 150000 /* Automatic BR detection ends at this value */

#define OPR_WRITE_MEM "wr"      /* Write To Memory/Flash */
#define OPR_READ_MEM "rd"       /* Read From Memory/Flash */
#define OPR_EXECUTE_EXIT "go"   /* Execute a non-return code */
#define OPR_EXECUTE_CONT "call" /* Execute returnable code */

enum sync_result {
	SR_OK = 0x00,
	SR_WRONG_DATA = 0x01,
	SR_TIMEOUT = 0x02,
	SR_ERROR = 0x03
};

/*----------------------------------------------------------------------------
 * External Variables
 *---------------------------------------------------------------------------
 */
extern struct comport_fields port_cfg;

/*---------------------------------------------------------------------------
 * Functions prototypes
 *---------------------------------------------------------------------------
 */

void opr_usage(void);
bool opr_close_port(void);
bool opr_open_port(const char *port_name, struct comport_fields port_cfg);
bool opr_write_chunk(uint8_t *buffer, uint32_t addr, uint32_t size);
bool opr_read_chunk(uint8_t *buffer, uint32_t addr, uint32_t size);
void opr_write_mem(uint8_t *buffer, uint32_t addr, uint32_t size);
void opr_read_mem(char *output, uint32_t addr, uint32_t size);
void opr_execute_exit(uint32_t addr);
bool opr_execute_return(uint32_t addr);
bool opr_scan_baudrate(void);
enum sync_result opr_check_sync(uint32_t baudrate);
#endif /* __UTIL_UUT_OPR_H */
