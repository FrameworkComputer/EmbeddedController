/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* JEDEC Serial Flash Discoverable Parameters (SFDP) for Serial NOR Flash,
 * covering v1.0 (JESD216) & v1.5 (JESD216A). */
#ifndef __CROS_EC_SFDP_H
#define __CROS_EC_SFDP_H

/**
 * Helper macros to declare and access SFDP defined bitfields at a JEDEC SFDP
 * defined double word (32b) granularity.
 */
#define SFDP_DEFINE_BITMASK_32(name, hi, lo) \
	static const uint32_t name =         \
		(((1ULL << ((hi) - (lo) + 1)) - 1UL) << (lo));
#define SFDP_DEFINE_SHIFT_32(name, hi, lo) static const size_t name = (lo);
#define SFDP_DEFINE_BITFIELD(name, hi, lo)          \
	SFDP_DEFINE_BITMASK_32(name##_MASK, hi, lo) \
	SFDP_DEFINE_SHIFT_32(name##_SHIFT, hi, lo)
#define SFDP_GET_BITFIELD(name, dw) (((dw) & name##_MASK) >> name##_SHIFT)

/**
 * Helper macros to construct SFDP defined double words (32b). Note reserved or
 * unused fields must always be set to all 1's.
 */
#define SFDP_BITFIELD(name, value) (((value) << name##_SHIFT) & name##_MASK)
#define SFDP_UNUSED(hi, lo) (((1ULL << ((hi) - (lo) + 1)) - 1UL) << (lo))

/******************************************************************************/
/* SFDP Header, always located at SFDP offset 0x0. Note that the SFDP space is
 * always read in 3 Byte addressing mode with a single cycle, where the
 * expected SFDP address space layout looks like the following:
 *
 *  ------------------0x00
 *  | SFDP Header        |  (specifying X number of Parameter Headers)
 *  ------------------0x08
 *  | Parameter Header 1 |  (specifying Y Parameter Table Pointer & Length L)
 *  ------------------0x10
 *          - - -
 *  --------------X * 0x08
 *  | Parameter Header X |  (specifying Z Parameter Table Pointer & Length K)
 *  --------(X + 1) * 0x08
 *          - - -
 *  ---------------------Y
 *  | Parameter Table 1  |
 *  -------------------Y+L
 *         - - -
 *  ---------------------Z            Key:    ------start_sfdp_offset
 *  | Parameter Table X  |                    | Region Name         |
 *  -------------------Z+K                    ------limit_sfdp_offset
 */

/*
 * SFDP Header 1st DWORD
 * ---------------------
 * <31:24> : Fourth signature byte == 'P'
 * <23:16> : Third signature byte  == 'D'
 * <15:8>  : Second signature byte == 'F'
 * <7:0>   : First signature byte  == 'S'
 */
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW1_P, 31, 24);
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW1_D, 23, 16);
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW1_F, 15, 8);
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW1_S, 7, 0);
#define SFDP_HEADER_DWORD_1(s, f, d, p)        \
	(SFDP_BITFIELD(SFDP_HEADER_DW1_P, p) | \
	 SFDP_BITFIELD(SFDP_HEADER_DW1_D, d) | \
	 SFDP_BITFIELD(SFDP_HEADER_DW1_F, f) | \
	 SFDP_BITFIELD(SFDP_HEADER_DW1_S, s))

#define SFDP_HEADER_DW1_SFDP_SIGNATURE_VALID(x) (x == 0x50444653)

/*
 * SFDP Header 2nd DWORD
 * ---------------------
 * <31:24> : Unused
 * <23:16> : Number of Parameter Headers (0-based, 0 indicates 1)
 * <15:8>  : SFDP Major Revision Number
 * <7:0>   : SFDP Minor Revision Number
 */
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW2_NPH, 23, 16);
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW2_SFDP_MAJOR, 15, 8);
SFDP_DEFINE_BITFIELD(SFDP_HEADER_DW2_SFDP_MINOR, 7, 0);
#define SFDP_HEADER_DWORD_2(nph, major, minor)                           \
	(SFDP_UNUSED(31, 24) | SFDP_BITFIELD(SFDP_HEADER_DW2_NPH, nph) | \
	 SFDP_BITFIELD(SFDP_HEADER_DW2_SFDP_MAJOR, major) |              \
	 SFDP_BITFIELD(SFDP_HEADER_DW2_SFDP_MINOR, minor))

/******************************************************************************/
/* SFDP v1.0 Parameter Headers, starts at SFDP offset 0x8 and there are as many
 * as specified in the v1.0 SFDP header. */

/* In SFDP v1.0, the only reserved ID was the Basic Flash Parameter Table ID of
 * 0x00. Otherwise this field must be set to the vendor's manufacturer ID. Note,
 * the spec does not call out how to report the manufacturer bank number. */
#define BASIC_FLASH_PARAMETER_TABLE_1_0_ID 0x00

/*
 * SFDP v1.0: Parameter Header 1st DWORD
 * --------------------------
 * <31:24> : Parameter Table Length (1-based, 1 indicates 1)
 * <23:16> : Parameter Table Major Revision Number
 * <15:8>  : Parameter Table Minor Revision Number
 * <7:0>   : ID number
 */
SFDP_DEFINE_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_PTL, 31, 24);
SFDP_DEFINE_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MAJOR, 23, 16);
SFDP_DEFINE_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MINOR, 15, 8);
SFDP_DEFINE_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_ID, 7, 0);
#define SFDP_1_0_PARAMETER_HEADER_DWORD_1(ptl, major, minor, id)           \
	(SFDP_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_PTL, ptl) |           \
	 SFDP_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MAJOR, major) | \
	 SFDP_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MINOR, minor) | \
	 SFDP_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_ID, id))

/*
 * SFDP v1.0: Parameter Header 2nd DWORD
 * --------------------------
 * <31:24> : Unused (0xFF)
 * <23:0>  : Parameter Table Pointer (SFDP offset which must be word aligned)
 */
SFDP_DEFINE_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW2_PTP, 23, 0);
#define SFDP_1_0_PARAMETER_HEADER_DWORD_2(ptp) \
	(SFDP_UNUSED(31, 24) |                 \
	 SFDP_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW2_PTP, ptp))

/******************************************************************************/
/* SFDP v1.5 Parameter Headers, starts at SFDP offset 0x8 and there are as many
 * as specified in the v1.5 SFDP header. */

/* Parameter ID MSB | Parameter ID LSB | Type                  | Owner
 * ==========================================================================
 * 0x00             | All              | Reserved              | JEDEC JC42.4
 * --------------------------------------------------------------------------
 * 0x01 - 0x7F      | odd parity       | JEDEC JEP106          | Vendor
 *                  |                  | Manufacturer ID       |
 *                  |                  | (mfn=LSB, bank=MSB)   |
 * --------------------------------------------------------------------------
 * 0x01 - 0x7F      | even parity      | Function Specific     | Vendor
 * --------------------------------------------------------------------------
 * 0x80 - 0xFE      | even parity      | Function Specific     | JEDEC JC42.4
 * --------------------------------------------------------------------------
 * 0xFF             | 0x00             | Basic Flash Parameter | JEDEC JC42.4
 *                  |                  | Table                 |
 * -------------------------------------------------------------------------- */

#define BASIC_FLASH_PARAMETER_TABLE_1_5_ID_MSB 0xFF
#define BASIC_FLASH_PARAMETER_TABLE_1_5_ID_LSB 0x00

/*
 * SFDP v1.5: Parameter Header 1st DWORD
 * --------------------------
 * <31:24> : Parameter Table Length (1-based, 1 indicates 1)
 * <23:16> : Parameter Table Major Revision Number
 * <15:8>  : Parameter Table Minor Revision Number
 * <7:0>   : Parameter ID LSB
 */
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_PTL, 31, 24);
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_TABLE_MAJOR, 23, 16);
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_TABLE_MINOR, 15, 8);
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_ID_LSB, 7, 0);
#define SFDP_1_5_PARAMETER_HEADER_DWORD_1(ptl, major, minor, idlsb)        \
	(SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_PTL, ptl) |           \
	 SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_TABLE_MAJOR, major) | \
	 SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_TABLE_MINOR, minor) | \
	 SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_ID_LSB, idlsb))

/*
 * SFDP v1.5: Parameter Header 2nd DWORD
 * --------------------------
 * <31:24> : Parameter ID MSB
 * <23:0>  : Parameter Table Pointer (SFDP offset which must be word aligned)
 */
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW2_ID_MSB, 31, 24);
SFDP_DEFINE_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW2_PTP, 23, 0);
#define SFDP_1_5_PARAMETER_HEADER_DWORD_2(idmsb, ptp)                 \
	(SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW2_ID_MSB, idmsb) | \
	 SFDP_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW2_PTP, ptp))

/******************************************************************************/
/* JEDEC (SPI Protocol) Basic Flash Parameter Table v1.0. The reporting of at
 * least one revision of this table is mandatory and must be specified by the
 * first parameter header.*/

/* Basic Flash Parameter Table v1.0 1st DWORD
 * ------------------------------------------
 * <31:23> : Unused
 * <22>    : Supports 1-1-4 Fast Read (1 if supported)
 * <21>    : Supports 1-4-4 Fast Read (1 if supported)
 * <20>    : Supports 1-2-2 Fast Read (1 if supported)
 * <19>    : Supports Double Transfer Rate (DTR) Clocking (1 if supported)
 * <18:17> : Address Bytes:
 *            - 0x0 if 3 Byte addressing only
 *            - 0x1 if defaults to 3B addressing, enters 4B on command
 *            - 0x2 if 4 Byte addressing only
 * <16>    : Supports 1-1-2 Fast Read (1 if supported)
 * <15:8>  : 4KiB Erase Opcode (0xFF if unsupported)
 * <7:5>   : Unused
 * <4>     : Write Enable Opcode Select for Writing to Volatile Status Register:
 *            - 0x0 if 0x50 is the opcode to enable a status register write
 *            - 0x1 if 0x06 is the opcode to enable a status register write
 * <3>     : Write Enable Instruction Required for writing to Volatile Status
 *           Register:
 *            - 0x0 if target flash only has nonvolatile status bits and does
 *              not require status register to be written every power on
 *            - 0x1 if target flash requires 0x00 to be written to the status
 *              register in order to allow writes and erases
 * <2>     : Write granularity (0 if the buffer is less than 64B, 1 if larger)
 * <1:0>   : Block/Sector Erase granularity available for the entirety of flash:
 *            - 0x1 if 4KiB is uniformly available
 *            - 0x3 if 4KiB is unavailable
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_1_1_4_SUPPORTED, 22, 22);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_1_4_4_SUPPORTED, 21, 21);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_1_2_2_SUPPORTED, 20, 20);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_DTR_SUPPORTED, 19, 19);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_ADDR_BYTES, 18, 17);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_1_1_2_SUPPORTED, 16, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_4KIB_ERASE_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_WREN_OPCODE_SELECT, 4, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_WREN_REQ, 3, 3);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_WRITE_GRANULARITY, 2, 2);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW1_4KIB_AVAILABILITY, 1, 0);
#define BFPT_1_0_DWORD_1(fr114, fr144, fr122, dtr, addr, fr112, rm4kb, wrenop, \
			 wrenrq, wrgr, ergr)                                   \
	(SFDP_UNUSED(31, 23) |                                                 \
	 SFDP_BITFIELD(BFPT_1_0_DW1_1_1_4_SUPPORTED, fr114) |                  \
	 SFDP_BITFIELD(BFPT_1_0_DW1_1_4_4_SUPPORTED, fr144) |                  \
	 SFDP_BITFIELD(BFPT_1_0_DW1_1_2_2_SUPPORTED, fr122) |                  \
	 SFDP_BITFIELD(BFPT_1_0_DW1_DTR_SUPPORTED, dtr) |                      \
	 SFDP_BITFIELD(BFPT_1_0_DW1_ADDR_BYTES, addr) |                        \
	 SFDP_BITFIELD(BFPT_1_0_DW1_1_1_2_SUPPORTED, fr112) |                  \
	 SFDP_BITFIELD(BFPT_1_0_DW1_4KIB_ERASE_OPCODE, rm4kb) |                \
	 SFDP_UNUSED(7, 5) |                                                   \
	 SFDP_BITFIELD(BFPT_1_0_DW1_WREN_OPCODE_SELECT, wrenop) |              \
	 SFDP_BITFIELD(BFPT_1_0_DW1_WREN_REQ, wrenrq) |                        \
	 SFDP_BITFIELD(BFPT_1_0_DW1_WRITE_GRANULARITY, wrgr) |                 \
	 SFDP_BITFIELD(BFPT_1_0_DW1_4KIB_AVAILABILITY, ergr))

/* Basic Flash Parameter Table v1.0 2nd DWORD
 * ------------------------------------------
 * <31>   : Density greater than 2 gibibits
 * <30:0> : N, where:
 *           - if =< 2 gibibits, flash memory density is N+1 bits
 *           - if > 2 gibibits, flash memory density is 2^N bits
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW2_GT_2_GIBIBITS, 31, 31);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW2_N, 30, 0);
#define BFPT_1_0_DWORD_2(gt_2_gibibits, n)                          \
	(SFDP_BITFIELD(BFPT_1_0_DW2_GT_2_GIBIBITS, gt_2_gibibits) | \
	 SFDP_BITFIELD(BFPT_1_0_DW2_N, n))

/* Basic Flash Parameter Table v1.0 3rd DWORD
 * ------------------------------------------
 * <31:24> : 1-1-4 Fast Read Opcode
 * <23:21> : 1-1-4 Fast Read Number of Mode Bits (0 if unsupported)
 * <20:16> : 1-1-4 Fast Read Number of Wait States (Wait State Clocks)
 * <15:8>  : 1-4-4 Fast Read Opcode
 * <7:5>   : 1-4-4 Fast Read Number of Mode Bits (0 if unsupported)
 * <4:0>   : 1-4-4 Fast Read Number of Wait States (Wait State CLocks)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_1_4_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_1_4_MODE_BITS, 23, 21);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_1_4_WAIT_STATE_CLOCKS, 20, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_4_4_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_4_4_MODE_BITS, 7, 5);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW3_1_4_4_WAIT_STATE_CLOCKS, 4, 0);
#define BFPT_1_0_DWORD_3(fr114op, fr114mb, fr114dc, fr144op, fr144mb, fr144dc) \
	(SFDP_BITFIELD(BFPT_1_0_DW3_1_1_4_OPCODE, fr114op) |                   \
	 SFDP_BITFIELD(BFPT_1_0_DW3_1_1_4_MODE_BITS, fr114mb) |                \
	 SFDP_BITFIELD(BFPT_1_0_DW3_1_1_4_WAIT_STATE_CLOCKS, fr114dc) |        \
	 SFDP_BITFIELD(BFPT_1_0_DW3_1_4_4_OPCODE, fr144op) |                   \
	 SFDP_BITFIELD(BFPT_1_0_DW3_1_4_4_MODE_BITS, fr144mb) |                \
	 SFDP_BITFIELD(BFPT_1_0_DW3_1_4_4_WAIT_STATE_CLOCKS, fr144dc))

/* Basic Flash Parameter Table v1.0 4th DWORD
 * ------------------------------------------
 * <31:24> : 1-2-2 Fast Read Opcode
 * <23:21> : 1-2-2 Fast Read Number of Mode Bits (0 if unsupported)
 * <20:16> : 1-2-2 Fast Read Number of Wait States (Wait State Clocks)
 * <15:8>  : 1-1-2 Fast Read Opcode
 * <7:5>   : 1-1-2 Fast Read Number of Mode Bits (0 if unsupported)
 * <4:0>   : 1-1-2 Fast Read Number of Wait States (Wait State CLocks)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_2_2_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_2_2_MODE_BITS, 23, 21);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_2_2_WAIT_STATE_CLOCKS, 20, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_1_2_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_1_2_MODE_BITS, 7, 5);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW4_1_1_2_WAIT_STATE_CLOCKS, 4, 0);
#define BFPT_1_0_DWORD_4(fr122op, fr122mb, fr122dc, fr112op, fr112mb, fr112dc) \
	(SFDP_BITFIELD(BFPT_1_0_DW4_1_2_2_OPCODE, fr122op) |                   \
	 SFDP_BITFIELD(BFPT_1_0_DW4_1_2_2_MODE_BITS, fr122mb) |                \
	 SFDP_BITFIELD(BFPT_1_0_DW4_1_2_2_WAIT_STATE_CLOCKS, fr122dc) |        \
	 SFDP_BITFIELD(BFPT_1_0_DW4_1_1_2_OPCODE, fr112op) |                   \
	 SFDP_BITFIELD(BFPT_1_0_DW4_1_1_2_MODE_BITS, fr112mb) |                \
	 SFDP_BITFIELD(BFPT_1_0_DW4_1_1_2_WAIT_STATE_CLOCKS, fr112dc))

/* Basic Flash Parameter Table v1.0 5th DWORD
 * ------------------------------------------
 * <31:5> : Reserved (0x7FFFFFF)
 * <4>    : Supports 4-4-4 Fast Read (1 if supported)
 * <3:1>  : Reserved (0x7)
 * <0>    : Supports 2-2-2 Fast Read (1 if supported)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW5_4_4_4_SUPPORTED, 4, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW5_2_2_2_SUPPORTED, 0, 0);
#define BFPT_1_0_DWORD_5(fr444, fr222)                        \
	(SFDP_UNUSED(31, 5) |                                 \
	 SFDP_BITFIELD(BFPT_1_0_DW5_4_4_4_SUPPORTED, fr444) | \
	 SFDP_UNUSED(3, 1) |                                  \
	 SFDP_BITFIELD(BFPT_1_0_DW5_2_2_2_SUPPORTED, fr222))

/* Basic Flash Parameter Table v1.0 6th DWORD
 * ------------------------------------------
 * <31:24> : 2-2-2 Fast Read Opcode
 * <23:21> : 2-2-2 Fast Read Number of Mode Bits (0 if unsupported)
 * <20:16> : 2-2-2 Fast Read Number of Wait States (Wait State Clocks)
 * <15:0>  : Reserved (0xFFFF)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW6_2_2_2_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW6_2_2_2_MODE_BITS, 23, 21);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW6_2_2_2_WAIT_STATE_CLOCKS, 20, 16);
#define BFPT_1_0_DWORD_6(fr222op, fr222mb, fr222dc)                     \
	(SFDP_BITFIELD(BFPT_1_0_DW6_2_2_2_OPCODE, fr222op) |            \
	 SFDP_BITFIELD(BFPT_1_0_DW6_2_2_2_MODE_BITS, fr222mb) |         \
	 SFDP_BITFIELD(BFPT_1_0_DW6_2_2_2_WAIT_STATE_CLOCKS, fr222dc) | \
	 SFDP_UNUSED(15, 0))

/* Basic Flash Parameter Table v1.0 7th DWORD
 * ------------------------------------------
 * <31:24> : 4-4-4 Fast Read Opcode
 * <23:21> : 4-4-4 Fast Read Number of Mode Bits (0 if unsupported)
 * <20:16> : 4-4-4 Fast Read Number of Wait States (Wait State Clocks)
 * <15:0>  : Reserved (0xFFFF)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW7_4_4_4_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW7_4_4_4_MODE_BITS, 23, 21);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW7_4_4_4_WAIT_STATE_CLOCKS, 20, 16);
#define BFPT_1_0_DWORD_7(fr444op, fr444mb, fr444dc)                     \
	(SFDP_BITFIELD(BFPT_1_0_DW7_4_4_4_OPCODE, fr444op) |            \
	 SFDP_BITFIELD(BFPT_1_0_DW7_4_4_4_MODE_BITS, fr444mb) |         \
	 SFDP_BITFIELD(BFPT_1_0_DW7_4_4_4_WAIT_STATE_CLOCKS, fr444dc) | \
	 SFDP_UNUSED(15, 0))

/* Basic Flash Parameter Table v1.0 8th DWORD
 * ------------------------------------------
 * <31:24> : Sector Type 2 Erase Opcode
 * <23:16> : Sector Type 2 Erase Size (2^N Bytes, 0 if unavailable)
 * <15:8>  : Sector Type 1 Erase Opcode
 * <7:0>   : Sector Type 1 Erase Size (2^N Bytes, 0 if unavailable)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_2_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_2_SIZE, 23, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_1_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_1_SIZE, 7, 0);
#define BFPT_1_0_DWORD_8(rm2op, rm2sz, rm1op, rm1sz)              \
	(SFDP_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_2_OPCODE, rm2op) | \
	 SFDP_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_2_SIZE, rm2sz) |   \
	 SFDP_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_1_OPCODE, rm1op) | \
	 SFDP_BITFIELD(BFPT_1_0_DW8_ERASE_TYPE_1_SIZE, rm1sz))

/* Basic Flash Parameter Table v1.0 9th DWORD
 * ------------------------------------------
 * <31:24> : Sector Type 4 Erase Opcode
 * <23:16> : Sector Type 4 Erase Size (2^N Bytes, 0 if unavailable)
 * <15:8>  : Sector Type 3 Erase Opcode
 * <7:0>   : Sector Type 3 Erase Size (2^N Bytes, 0 if unavailable)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_4_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_4_SIZE, 23, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_3_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_3_SIZE, 7, 0);
#define BFPT_1_0_DWORD_9(rm4op, rm4sz, rm3op, rm3sz)              \
	(SFDP_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_4_OPCODE, rm4op) | \
	 SFDP_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_4_SIZE, rm4sz) |   \
	 SFDP_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_3_OPCODE, rm3op) | \
	 SFDP_BITFIELD(BFPT_1_0_DW9_ERASE_TYPE_3_SIZE, rm3sz))

/******************************************************************************/
/* JEDEC (SPI Protocol) Basic Flash Parameter Table v1.5. The reporting of at
 * least one revision of this table is mandatory and must be specified by the
 * first parameter header. Note that DWORDs 1-9 are identical to v1.0. */

/* Basic Flash Parameter Table v1.5 10th DWORD
 * ------------------------------------------
 * <31:30> : Sector Type 4 Erase, Typical time units, where
 *           0x0: 1ms, 0x1: 16ms, 0x2: 128ms, 0x3: 1s
 * <29:25> : Sector Type 4 Erase, Typical time count, where
 *           time = (count + 1) * units
 * <24:23> : Sector Type 3 Erase, Typical time units, where
 *           0x0: 1ms, 0x1: 16ms, 0x2: 128ms, 0x3: 1s
 * <22:18> : Sector Type 3 Erase, Typical time count, where
 *           time = (count + 1) * units
 * <17:16> : Sector Type 2 Erase, Typical time units, where
 *           0x0: 1ms, 0x1: 16ms, 0x2: 128ms, 0x3: 1s
 * <15:11> : Sector Type 2 Erase, Typical time count, where
 *           time = (count + 1) * units
 * <10:9>  : Sector Type 1 Erase, Typical time units, where
 *           0x0: 1ms, 0x1: 16ms, 0x2: 128ms, 0x3: 1s
 * <8:4>   : Sector Type 1 Erase, Typical time count, where
 *           time = (count + 1) * units
 * <3:0>   : Multiplier from typical to maximum erase time, where
 *           maximum_time = 2 * (multiplier + 1) * typical_time
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_4_TIME_UNIT, 31, 30);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_4_TIME_CNT, 29, 25);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_3_TIME_UNIT, 24, 23);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_3_TIME_CNT, 22, 18);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_2_TIME_UNIT, 17, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_2_TIME_CNT, 15, 11);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_1_TIME_UNIT, 10, 9);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_1_TIME_CNT, 8, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW10_ERASE_TIME_MAX_MULT, 3, 0);
#define BFPT_1_5_DWORD_10(rm4unit, rm4count, rm3unit, rm3count, rm2unit, \
			  rm2count, rm1unit, rm1count, maxmult)          \
	(SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_4_TIME_UNIT, rm4unit) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_4_TIME_CNT, rm4count) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_3_TIME_UNIT, rm3unit) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_3_TIME_CNT, rm3count) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_2_TIME_UNIT, rm2unit) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_2_TIME_CNT, rm2count) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_1_TIME_UNIT, rm1unit) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_1_TIME_CNT, rm1count) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW10_ERASE_TIME_MAX_MULT, maxmult))

/* Basic Flash Parameter Table v1.5 11th DWORD
 * ------------------------------------------
 * <31>    : Reserved (0x1)
 * <30:29> : Chip Erase, Typical time units, where
 *           0x0: 16ms, 0x1: 256ms, 0x2: 4s, 0x3: 64s
 * <28:24> : Chip Erase, Typical time count, where time = (count + 1) * units
 * <23>    : Additional Byte Program, Typical time units (0: 1us, 1: 8us)
 * <22:19> : Additional Byte Program, Typical time count, where each byte takes
 *           time = (count + 1) * units * bytes. This should not be
 *           used if the additional bytes count exceeds 1/2 a page size.
 * <18>    : First Byte Program, Typical time units (0: 1us, 1: 8us)
 * <17:14> : First Byte Program, Typical time count, where each byte takes
 *           time = (count + 1) * units * bytes
 * <13>    : Page Program, Typical time units (0: 8us, 1: 64us)
 * <12:8>  : Page Program, Typical time count, where time = (count + 1) * units
 * <7:4>   : Page Size (2^N Bytes)
 * <3:0>   : Multiplier from typical time to max time for programming, where
 *           maximum_time = 2 * (multiplier + 1) * typical_time
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_CHIP_ERASE_TIME_UNIT, 30, 29);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_CHIP_ERASE_TIME_CNT, 28, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_MORE_BYTE_WR_TIME_UNIT, 23, 23);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_MORE_BYTE_WR_TIME_CNT, 22, 19);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_INIT_BYTE_WR_TIME_UNIT, 18, 18);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_INIT_BYTE_WR_TIME_CNT, 17, 14);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_PAGE_WR_TIME_UNIT, 13, 13);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_PAGE_WR_TIME_CNT, 12, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_PAGE_SIZE, 7, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW11_WR_TIME_MAX_MULT, 3, 0);
#define BFPT_1_5_DWORD_11(crmunit, crmcount, mrbunit, mrbcount, initunit,  \
			  initcount, pgwrunit, pgwrcount, pagesz, maxmult) \
	(SFDP_UNUSED(31, 31) |                                             \
	 SFDP_BITFIELD(BFPT_1_5_DW11_CHIP_ERASE_TIME_UNIT, crmunit) |      \
	 SFDP_BITFIELD(BFPT_1_5_DW11_CHIP_ERASE_TIME_CNT, crmcount) |      \
	 SFDP_BITFIELD(BFPT_1_5_DW11_MORE_BYTE_WR_TIME_UNIT, mrbunit) |    \
	 SFDP_BITFIELD(BFPT_1_5_DW11_MORE_BYTE_WR_TIME_CNT, mrbcount) |    \
	 SFDP_BITFIELD(BFPT_1_5_DW11_INIT_BYTE_WR_TIME_UNIT, initunit) |   \
	 SFDP_BITFIELD(BFPT_1_5_DW11_INIT_BYTE_WR_TIME_CNT, initcount) |   \
	 SFDP_BITFIELD(BFPT_1_5_DW11_PAGE_WR_TIME_UNIT, pgwrunit) |        \
	 SFDP_BITFIELD(BFPT_1_5_DW11_PAGE_WR_TIME_CNT, pgwrcount) |        \
	 SFDP_BITFIELD(BFPT_1_5_DW11_PAGE_SIZE, pagesz) |                  \
	 SFDP_BITFIELD(BFPT_1_5_DW11_WR_TIME_MAX_MULT, maxmult))

/* Basic Flash Parameter Table v1.5 12th DWORD
 * ------------------------------------------
 * <31>    : Suspend / Resume unsupported (1 unsupported, 0 supported)
 * <30:29> : Suspend in-progress erase max latency units, where
 *           0x0: 128ns, 0x1: 1us, 0x2: 8us, 0x3: 64us
 * <28:24> : Suspend in-progress erase max latency count, where
 *           max latency = (count + 1) * units
 * <23:20> : Erase resume to suspend minimum interval, (count + 1) * 64us
 * <19:18> : Suspend in-progress program max latency units, where
 *           0x0: 128ns, 0x1: 1us, 0x2: 8us, 0x3: 64us
 * <17:13> : Suspend in-progress program max latency count, where
 *           max latency = (count + 1) * units
 * <12:9>  : Program resume to suspend minimum internal, (count + 1) * 64us
 * <8>     : Reserved (0x1)
 * <7:4>   : Prohibited Operations During Erase Suspend flags, where
 *           xxx0b May not initiate a new erase anywhere
 *                 (erase nesting not permitted)
 *           xxx1b May not initiate a new erase in the erase suspended sector
 *                 size
 *           xx0xb May not initiate a page program anywhere
 *           xx1xb May not initiate a page program in the erase suspended
 *                 sector size
 *           x0xxb Refer to vendor datasheet for read restrictions
 *           x1xxb May not initiate a read in the erase suspended sector size
 *           0xxxb Additional erase or program restrictions apply
 *           1xxxb The erase and program restrictions in bits 5:4 are
 *                 sufficient
 * <3:0>   : Prohibited Operations During Program Suspend flags, where
 *           xxx0b May not initiate a new erase anywhere
 *                 (erase nesting not permitted)
 *           xxx1b May not initiate a new erase in the program suspended page
 *                 size
 *           xx0xb May not initiate a new page program anywhere
 *                 (program nesting not permitted)
 *           xx1xb May not initiate a new page program in the program suspended
 *                 page size
 *           x0xxb Refer to vendor datasheet for read restrictions
 *           x1xxb May not initiate a read in the program suspended page size
 *           0xxxb Additional erase or program restrictions apply
 *           1xxxb The erase and program restrictions in bits 1:0 are
 *                 sufficient
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_SUSPEND_UNSUPPORTED, 31, 31);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_SUSP_RM_MAX_LAT_UNIT, 30, 29);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_SUSP_RM_MAX_LAT_CNT, 28, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_RM_RES_TO_SUSP_LAT_CNT, 23, 20);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_SUSP_WR_MAX_LAT_UNIT, 19, 18)
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_SUSP_WR_MAX_LAT_CNT, 17, 13);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_WR_RES_TO_SUSP_LAT_CNT, 12, 9);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_PROHIB_OPS_DURING_RM_SUSP, 7, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW12_PROHIB_OPS_DURING_WR_SUSP, 3, 0);
#define BFPT_1_5_DWORD_12(unsup, susprmlatun, susprmlatcnt, rmressusplatcnt,   \
			  suspwrmaxlatunit, suspwrmaxlatcnt, wrressuspcnt,     \
			  prohibopsrmsusp, prohibopswrsusp)                    \
	(SFDP_BITFIELD(BFPT_1_5_DW12_SUSPEND_UNSUPPORTED, unsup) |             \
	 SFDP_BITFIELD(BFPT_1_5_DW12_SUSP_RM_MAX_LAT_UNIT, susprmlatun) |      \
	 SFDP_BITFIELD(BFPT_1_5_DW12_SUSP_RM_MAX_LAT_CNT, susprmlatcnt) |      \
	 SFDP_BITFIELD(BFPT_1_5_DW12_RM_RES_TO_SUSP_LAT_CNT,                   \
		       rmressusplatcnt) |                                      \
	 SFDP_BITFIELD(BFPT_1_5_DW12_SUSP_WR_MAX_LAT_UNIT, suspwrmaxlatunit) | \
	 SFDP_BITFIELD(BFPT_1_5_DW12_SUSP_WR_MAX_LAT_CNT, suspwrmaxlatcnt) |   \
	 SFDP_BITFIELD(BFPT_1_5_DW12_WR_RES_TO_SUSP_LAT_CNT, wrressuspcnt) |   \
	 SFDP_UNUSED(8, 8) |                                                   \
	 SFDP_BITFIELD(BFPT_1_5_DW12_PROHIB_OPS_DURING_RM_SUSP,                \
		       prohibopsrmsusp) |                                      \
	 SFDP_BITFIELD(BFPT_1_5_DW12_PROHIB_OPS_DURING_WR_SUSP,                \
		       prohibopswrsusp))

/* Basic Flash Parameter Table v1.5 13th DWORD
 * ------------------------------------------
 * <31:24> : Suspend Instruction used to suspend a write or erase type operation
 * <23:16> : Resume Instruction used to resume a write or erase type operation
 * <15:8>  : Program Suspend Instruction used to suspend a program operation
 * <7:0>   : Program Resume Instruction used to resume a program operation
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW13_SUSPEND_OPCODE, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW13_RESUME_OPCODE, 23, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW13_WR_SUSPEND_OPCODE, 15, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW13_WR_RESUME_OPCODE, 7, 0);
#define BFPT_1_5_DWORD_13(suspop, resop, wrsspop, wrresop)         \
	(SFDP_BITFIELD(BFPT_1_5_DW13_SUSPEND_OPCODE, suspop) |     \
	 SFDP_BITFIELD(BFPT_1_5_DW13_RESUME_OPCODE, resop) |       \
	 SFDP_BITFIELD(BFPT_1_5_DW13_WR_SUSPEND_OPCODE, wrsspop) | \
	 SFDP_BITFIELD(BFPT_1_5_DW13_WR_RESUME_OPCODE, wrresop))

/* Basic Flash Parameter Table v1.5 14th DWORD
 * ------------------------------------------
 * <31>    : Deep powerdown unsupported (1 unsupported, 0 supported)
 * <30:23> : Enter deep powerdown instruction
 * <22:15> : Exit deep powerdown instruction
 * <14:13> : Exit deep powerdown to next operation delay units, where
 *           0x0: 128ns, 0x1: 1us, 0x2: 8us, 0x3: 64us
 * <12:8>  : Exit deep powerdown to next operation delay count, where
 *           delay = = (count + 1) * units
 * <7:2>   : Status Register Polling Device Busy Flags, where
 *           xx_xx1xb Bit 7 of the Flag Status Register may be polled any time
 *                    a Program, Erase, Suspend/Resume command is issued, or
 *                    after a Reset command while the device is busy. The read
 *                    instruction is 70h. Flag Status Register bit definitions:
 *                    bit[7]: Program or erase controller status
 *                    (0=busy; 1=ready)
 *           xx_xxx1b Use of legacy polling is supported by reading the Status
 *                    Register with 05h instruction and checking WIP bit[0]
 *                    (0=ready; 1=busy).
 * <1:0>   : Reserved (0x3)
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_POWER_DOWN_UNSUPPORTED, 31, 31);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_POWER_DOWN_OPCODE, 30, 23);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_POWER_UP_OPCODE, 22, 15);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_POWER_UP_TIME_UNIT, 14, 13);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_POWER_UP_TIME_CNT, 12, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW14_BUSY_FLAGS, 7, 2);
#define BFPT_1_5_DWORD_14(pwrdwnunsup, pwrdwnop, pwrupop, pwrupunit, pwrupcnt, \
			  busypollflags)                                       \
	(SFDP_BITFIELD(BFPT_1_5_DW14_POWER_DOWN_UNSUPPORTED, pwrdwnunsup) |    \
	 SFDP_BITFIELD(BFPT_1_5_DW14_POWER_DOWN_OPCODE, pwrdwnop) |            \
	 SFDP_BITFIELD(BFPT_1_5_DW14_POWER_UP_OPCODE, pwrupop) |               \
	 SFDP_BITFIELD(BFPT_1_5_DW14_POWER_UP_TIME_UNIT, pwrupunit) |          \
	 SFDP_BITFIELD(BFPT_1_5_DW14_POWER_UP_TIME_CNT, pwrupcnt) |            \
	 SFDP_BITFIELD(BFPT_1_5_DW14_BUSY_FLAGS, busypollflags) |              \
	 SFDP_UNUSED(1, 0))

/* Basic Flash Parameter Table v1.5 15th DWORD
 * ------------------------------------------
 * <31:24> : Reserved (0xFF)
 * <23>    : HOLD and WIP disable supported by setting the non-volatile extended
 *           configuration register's bit 4 to 0.
 * <22:20> : Quad Enable Requirements (1-1-4, 1-4-4, 4-4-4 Fast Reads), where
 *           000b Device does not have a QE bit. Device detects 1-1-4 and 1-4-4
 *                reads based on instruction. DQ3/HOLD# functions as hold during
 *                instruction phase.
 *           001b QE is bit 1 of status register 2. It is set via Write Status
 *                with two data bytes where bit 1 of the second byte is one. It
 *                is cleared via Write Status with two data bytes where bit
 *                1 of the second byte is zero. Writing only one byte to the
 *                status register has the side-effect of clearing status
 *                register 2, including the QE bit. The 100b code is used if
 *                writing one byte to the status register does not modify status
 *                register 2.
 *           010b QE is bit 6 of status register 1. It is set via Write Status
 *                with one data byte where bit 6 is one. It is cleared via Write
 *                Status with one data byte where bit 6 is zero.
 *           011b QE is bit 7 of status register 2. It is set via Write status
 *                register 2 instruction 3Eh with one data byte where bit 7 is
 *                one. It is cleared via Write status register 2 instruction
 *                3Eh with one data byte where bit 7 is zero. The status
 *                register 2 is read using instruction 3Fh.
 *           100b QE is bit 1 of status register 2. It is set via Write Status
 *                with two data bytes where bit 1 of the second byte is one. It
 *                is cleared via Write Status with two data bytes where bit 1
 *                of the second byte is zero. In contrast to the 001b code,
 *                writing one byte to the status register does not modify status
 *                register 2.
 *           101b QE is bit 1 of the status register 2. Status register 1 is
 *                read using Read Status instruction 05h. Status register 2 is
 *                read using instruction 35h. QE is set via Write Status
 *                instruction 01h with two data bytes where bit 1 of the second
 *                byte is one. It is cleared via Write Status with two data
 *                bytes where bit 1 of the second byte is zero.
 * <19:16> : 0-4-4 Mode Entry Method, where
 *           xxx1b Mode Bits[7:0] = A5h Note: QE must be set prior to using this
 *                 mode
 *           xx1xb Read the 8-bit volatile configuration register with
 *                 instruction 85h, set XIP bit[3] in the data read, and write
 *                 the modified data using the instruction 81h, then Mode Bits
 *                 [7:0] = 01h
 * <15:10> : 0-4-4 Mode Exit Method, where
 *           xx_xxx1b Mode Bits[7:0] = 00h will terminate this mode at the end
 *                    of the current read operation
 *           xx_xx1xb If 3-Byte address active, input Fh on DQ0-DQ3 for 8
 *                    clocks. If 4-Byte address active, input Fh on DQ0-DQ3 for
 *                    10 clocks. This will terminate the mode prior to the next
 *                    read operation.
 *           xx_1xxxb Input Fh (mode bit reset) on DQ0-DQ3 for 8 clocks. This
 *                    will terminate the mode prior to the next read operation.
 * <9>     : 0-4-4 mode supported (1 supported, 0 unsupported)
 * <8:4>   : 4-4-4 mode enable sequences, where
 *           x_xxx1b set QE per QER description above, then issue
 *                   instruction 38h
 *           x_xx1xb issue instruction 38h
 *           x_x1xxb issue instruction 35h
 *           x_1xxxb device uses a read-modify-write sequence of operations:
 *                   read configuration using instruction 65h followed by
 *                   address 800003h, set bit 6,
 *                   write configuration using instruction 71h followed by
 *                   address 800003h. This configuration is volatile.
 * <3:0>   : 4-4-4 mode disable sequences, where
 *           xxx1b issue FFh instruction
 *           xx1xb issue F5h instruction
 *           x1xxb device uses a read-modify-write sequence of operations:
 *                 read configuration using instruction 65h followed by address
 *                 800003h, clear bit 6,
 *                 write configuration using instruction 71h followed by
 *                 address 800003h. This configuration is volatile.
 *           1xxxb issue the Soft Reset 66/99 sequence
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_HOLD_WP_DISABLE, 23, 23);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_QE_REQ, 22, 20);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_0_4_4_ENTRY, 19, 16);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_0_4_4_EXIT, 15, 10);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_0_4_4_SUPPORTED, 9, 9);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_4_4_4_ENTRY, 8, 4);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW15_4_4_4_EXIT, 3, 0);
#define BFPT_1_5_DWORD_15(holdwpdis, qereq, fr044entry, fr044exit, fr044sup, \
			  fr444entry, fr444exit)                             \
	(SFDP_UNUSED(31, 24) |                                               \
	 SFDP_BITFIELD(BFPT_1_5_DW15_HOLD_WP_DISABLE, holdwpdis) |           \
	 SFDP_BITFIELD(BFPT_1_5_DW15_QE_REQ, qereq) |                        \
	 SFDP_BITFIELD(BFPT_1_5_DW15_0_4_4_ENTRY, fr044entry) |              \
	 SFDP_BITFIELD(BFPT_1_5_DW15_0_4_4_EXIT, fr044exit) |                \
	 SFDP_BITFIELD(BFPT_1_5_DW15_0_4_4_SUPPORTED, fr044sup) |            \
	 SFDP_BITFIELD(BFPT_1_5_DW15_4_4_4_ENTRY, fr444entry) |              \
	 SFDP_BITFIELD(BFPT_1_5_DW15_4_4_4_EXIT, fr444exit))

/* Basic Flash Parameter Table v1.5 16th DWORD
 * -------------------------------------------
 * <31:24> : Enter 4-Byte Addressing, where
 *           xxxx_xxx1b issue instruction B7h
 *                      (preceding write enable not required)
 *           xxxx_xx1xb issue write enable instruction 06h, then issue
 *                      instruction B7h
 *           xxxx_x1xxb 8-bit volatile extended address register used to define
 *                      A[31:24] bits. Read with instruction C8h. Write
 *                      instruction is C5h with 1 byte of data. Select the
 *                      active 128 Mbit memory segment by setting the
 *                      appropriate A[31:24] bits and use 3-Byte addressing.
 *           xxxx_1xxxb 8-bit volatile bank register used to define A[30:A24]
 *                      bits. MSB (bit[7]) is used to enable/disable 4-byte
 *                      address mode. When MSB is set to ‘1’, 4-byte address
 *                      mode is active and A[30:24] bits are don’t care. Read
 *                      with instruction 16h. Write instruction is 17h with 1
 *                      byte of data. When MSB is cleared to ‘0’, select the
 *                      active 128 Mbit segment by setting the appropriate
 *                      A[30:24] bits and use 3-Byte addressing.
 *           xxx1_xxxxb A 16-bit nonvolatile configuration register controls
 *                      3-Byte/4-Byte address mode. Read instruction is B5h.
 *                      Bit[0] controls address mode [0=3-Byte; 1=4-Byte]. Write
 *                      configuration register instruction is B1h, data length
 *                      is 2 bytes.
 *           xx1x_xxxxb Supports dedicated 4-Byte address instruction set.
 *                      Consult vendor data sheet for the instruction set
 *                      definition.
 *           x1xx_xxxxb Always operates in 4-Byte address mode
 * <23:14> : Exit 4-Byte Addressing, where
 *           xx_xxxx_xxx1b issue instruction E9h to exit 4-Byte address mode
 *                         (write enable instruction 06h is not required)
 *           xx_xxxx_xx1xb issue write enable instruction 06h, then issue
 *                         instruction E9h to exit 4-Byte address mode
 *           xx_xxxx_x1xxb 8-bit volatile extended address register used to
 *                         define A[31:A24] bits. Read with instruction C8h.
 *                         Write instruction is C5h, data length is 1 byte.
 *                         Return to lowest memory segment by setting A[31:24]
 *                         to 00h and use 3-Byte addressing.
 *           xx_xxxx_1xxxb 8-bit volatile bank register used to define A[30:A24]
 *                         bits. MSB (bit[7]) is used to enable/disable 4-byte
 *                         address mode. When MSB is cleared to ‘0’, 3-byte
 *                         address mode is active and A30:A24 are used to select
 *                         the active 128 Mbit memory segment. Read with
 *                         instruction 16h. Write instruction is 17h, data
 *                         length is 1 byte.
 *           xx_xxx1_xxxxb A 16-bit nonvolatile configuration register controls
 *                         3-Byte/4-Byte address mode. Read instruction is B5h.
 *                         Bit[0] controls address mode [0=3-Byte; 1=4-Byte].
 *                         Write configuration register instruction is B1h, data
 *                         length is 2 bytes.
 *           xx_xx1x_xxxxb Hardware reset
 *           xx_x1xx_xxxxb Software reset (see bits 13:8 in this DWORD)
 *           xx_1xxx_xxxxb Power cycle
 * <13:8>  : Soft Reset and Rescue Sequence Support, where
 *           00_0000b no software reset instruction is supported
 *           xx_xxx1b drive Fh on all 4 data wires for 8 clocks
 *           xx_xx1xb drive Fh on all 4 data wires for 10 clocks if device is
 *                    operating in 4-byte address mode
 *           xx_x1xxb drive Fh on all 4 data wires for 16 clocks
 *           xx_1xxxb issue instruction F0h
 *           x1_xxxxb issue reset enable instruction 66h, then issue reset
 *                    instruction 99h. The reset enable, reset sequence may be
 *                    issued on 1, 2, or 4 wires depending on the device
 *                    operating mode.
 *           1x_xxxxb exit 0-4-4 mode is required prior to other reset sequences
 *                    above if the device may be operating in this mode.
 * <7>     : Reserved (0x1)
 * <6:0>   : Volatile or Non-Volatile Register and Write Enable Instruction for
 *           Status Register 1, where
 *           xx0_0000b status register is read only
 *           xxx_xxx1b Non-Volatile Status Register 1, powers-up to last written
 *                     value, use instruction 06h to enable write
 *           xxx_xx1xb Volatile Status Register 1, status register powers-up
 *                     with bits set to "1"s, use instruction 06h to enable
 *                     write
 *           xxx_x1xxb Volatile Status Register 1, status register powers-up
 *                     with bits set to "1"s, use instruction 50h to enable
 *                     write
 *           xxx_1xxxb Non-Volatile/Volatile status register 1 powers-up to last
 *                     written value in the non-volatile status register, use
 *                     instruction 06h to enable write to non-volatile status
 *                     register. Volatile status register may be activated after
 *                     power-up to override the non-volatile status register,
 *                     use instruction 50h to enable write and activate the
 *                     volatile status register.
 *           xx1_xxxxb Status Register 1 contains a mix of volatile and
 *                     non-volatile bits. The 06h instruction is used to enable
 *                     writing of the register.
 */
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW16_4_BYTE_ENTRY, 31, 24);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW16_4_BYTE_EXIT, 23, 14);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW16_SOFT_RESET, 13, 8);
SFDP_DEFINE_BITFIELD(BFPT_1_5_DW16_STATUS_REG_1, 6, 0);
#define BFPT_1_5_DWORD_16(entry, exit, softreset, statusreg1) \
	(SFDP_BITFIELD(BFPT_1_5_DW16_4_BYTE_ENTRY, entry) |   \
	 SFDP_BITFIELD(BFPT_1_5_DW16_4_BYTE_EXIT, exit) |     \
	 SFDP_BITFIELD(BFPT_1_5_DW16_SOFT_RESET, softreset) | \
	 SFDP_UNUSED(7, 7) |                                  \
	 SFDP_BITFIELD(BFPT_1_5_DW16_STATUS_REG_1, statusreg1))

#endif /* __CROS_EC_SFDP_H */
