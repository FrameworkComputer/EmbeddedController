/*
 * Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ECST_H
#define ECST_H

/*---------------------------------------------------------------------------
  Includes
  --------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

/*---------------------------------------------------------------------------
  Defines
  --------------------------------------------------------------------------*/

/* For the beauty */
#define TRUE 1
#define FALSE 0

/* CHANGEME when the version is updated */
#define T_VER 1
#define T_REV_MAJOR 0
#define T_REV_MINOR 3

/* Header starts by default at 0x20000 */
#define FIRMWARE_OFFSET_FROM_HEADER 0x40

#define ARM_FW_ENTRY_POINT_OFFSET 0x04

/* Some useful offsets inside the header */
#define HDR_ANCHOR_OFFSET 0
#define HDR_EXTENDED_ANCHOR_OFFSET 4
#define HDR_SPI_MAX_CLK_OFFSET 6
#define HDR_SPI_READ_MODE_OFFSET 7
#define HDR_ERR_DETECTION_CONF_OFFSET 8
#define HDR_FW_LOAD_START_ADDR_OFFSET 9
#define HDR_FW_ENTRY_POINT_OFFSET 13
#define HDR_FW_ERR_DETECT_START_ADDR_OFFSET 17
#define HDR_FW_ERR_DETECT_END_ADDR_OFFSET 21
#define HDR_FW_LENGTH_OFFSET 25
#define HDR_FLASH_SIZE_OFFSET 29
#define HDR_RESERVED 30
#define HDR_FW_HEADER_SIG_OFFSET 56
#define HDR_FW_IMAGE_SIG_OFFSET 60

#define FIRMW_CKSM_OFFSET 0x3C

/* Header field known values */
#define FW_HDR_ANCHOR 0x2A3B4D5E
#define FW_HDR_EXT_ANCHOR_ENABLE 0xAB1E
#define FW_HDR_EXT_ANCHOR_DISABLE 0x54E1
#define FW_CRC_DISABLE 0x00
#define FW_CRC_ENABLE 0x02
#define HEADER_CRC_FIELDS_SIZE 8

#define HDR_PTR_SIGNATURE 0x55AA650E

#define CKSMCRC_INV_BIT_OFFSET 0x1

/* Some common Sizes */
#define STR_SIZE 200
#define ARG_SIZE 100
#define NAME_SIZE 160
#define BUFF_SIZE 0x400
#define HEADER_SIZE 64
#define TMP_STR_SIZE 21
#define PAD_VALUE 0x00

#define MAX_ARGS 100

/* Text Colors */
#define TDBG 0x02 /* Dark Green     */
#define TPAS 0x0A /* light green    */
#define TINF 0x0B /* light turquise */
#define TERR 0x0C /* light red      */
#define TUSG 0x0E /* light yellow   */

/* Indicates bin Command line parameters */
#define BIN_FW_HDR_CRC_DISABLE 0x0001
#define BIN_FW_CRC_DISABLE 0x0002
#define BIN_FW_START 0x0004
#define BIN_FW_SIZE 0x0008
#define BIN_CK_FIRMWARE 0x0010
#define BIN_FW_CKS_START 0x0020
#define BIN_FW_CKS_SIZE 0x0040
#define BIN_FW_CHANGE_SIG 0x0080
#define BIN_FW_SPI_MAX_CLK 0x0100
#define BIN_FW_LOAD_START_ADDR 0x0200
#define BIN_FW_ENTRY_POINT 0x0400
#define BIN_FW_LENGTH 0x0800
#define BIN_FW_HDR_OFFSET 0x1000
#define BIN_FW_USER_ARM_RESET 0x2000
#define BIN_UNLIM_BURST_ENABLE 0x4000

#define ECRP_OFFSET 0x01
#define ECRP_INPUT_FILE 0x02
#define ECRP_OUTPUT_FILE 0x04

#define SPI_MAX_CLOCK_20_MHZ_VAL 20
#define SPI_MAX_CLOCK_25_MHZ_VAL 25
#define SPI_MAX_CLOCK_33_MHZ_VAL 33
#define SPI_MAX_CLOCK_40_MHZ_VAL 40
#define SPI_MAX_CLOCK_50_MHZ_VAL 50

#define SPI_MAX_CLOCK_20_MHZ 0x00
#define SPI_MAX_CLOCK_25_MHZ 0x01
#define SPI_MAX_CLOCK_33_MHZ 0x02
#define SPI_MAX_CLOCK_40_MHZ 0x03
#define SPI_MAX_CLOCK_50_MHZ 0x04
#define SPI_MAX_CLOCK_MASK 0xF8

#define SPI_CLOCK_RATIO_1_VAL 1
#define SPI_CLOCK_RATIO_2_VAL 2

#define SPI_CLOCK_RATIO_1 0x07
#define SPI_CLOCK_RATIO_2 0x08

#define SPI_NORMAL_MODE_VAL "normal"
#define SPI_SINGLE_MODE_VAL "fast"
#define SPI_DUAL_MODE_VAL "dual"
#define SPI_QUAD_MODE_VAL "quad"

#define SPI_NORMAL_MODE 0x00
#define SPI_SINGLE_MODE 0x01
#define SPI_DUAL_MODE 0x03
#define SPI_QUAD_MODE 0x04

#define SPI_UNLIMITED_BURST_ENABLE 0x08

#define FLASH_SIZE_1_MBYTES_VAL 1
#define FLASH_SIZE_2_MBYTES_VAL 2
#define FLASH_SIZE_4_MBYTES_VAL 4
#define FLASH_SIZE_8_MBYTES_VAL 8
#define FLASH_SIZE_16_MBYTES_VAL 16

#define FLASH_SIZE_1_MBYTES 0x01
#define FLASH_SIZE_2_MBYTES 0x03
#define FLASH_SIZE_4_MBYTES 0x07
#define FLASH_SIZE_8_MBYTES 0x0F
#define FLASH_SIZE_16_MBYTES 0x1F

/* Header fields default values. */
#define SPI_MAX_CLOCK_DEFAULT SPI_MAX_CLOCK_20_MHZ_VAL
#define SPI_READ_MODE_DEFAULT SPI_NORMAL_MODE
#define FLASH_SIZE_DEFAULT FLASH_SIZE_16_MBYTES_VAL
#define FW_CRC_START_ADDR 0x00000000

#define ADDR_16_BYTES_ALIGNED_MASK 0x0000000F
#define ADDR_4_BYTES_ALIGNED_MASK 0x00000003

#define MAX_FLASH_SIZE 0x03ffffff

/* Chips: convert from name to index. */
enum npcx_chip_ram_variant {
	NPCX5M5G = 0,
	NPCX5M6G = 1,
	NPCX7M5 = 2,
	NPCX7M6 = 3,
	NPCX7M7 = 4,
	NPCX9M3 = 5,
	NPCX9M6 = 6,
	NPCX9M7 = 7,
	NPCX9MFP = 8,
	NPCX_CHIP_RAM_VAR_NONE
};

#define DEFAULT_CHIP NPCX5M5G

/* NPCX5 */
#define NPCX5M5G_RAM_ADDR 0x100A8000
#define NPCX5M5G_RAM_SIZE 0x20000
#define NPCX5M6G_RAM_ADDR 0x10088000
#define NPCX5M6G_RAM_SIZE 0x40000
/* NPCX7 */
#define NPCX7M5X_RAM_ADDR 0x100A8000
#define NPCX7M5X_RAM_SIZE 0x20000
#define NPCX7M6X_RAM_ADDR 0x10090000
#define NPCX7M6X_RAM_SIZE 0x40000
#define NPCX7M7X_RAM_ADDR 0x10070000
#define NPCX7M7X_RAM_SIZE 0x60000
/* NPCX9 */
#define NPCX9M3X_RAM_ADDR 0x10080000
#define NPCX9M3X_RAM_SIZE 0x50000
#define NPCX9M6X_RAM_ADDR 0x10090000
#define NPCX9M6X_RAM_SIZE 0x40000
#define NPCX9MFP_RAM_ADDR 0x10058000
#define NPCX9MFP_RAM_SIZE 0x80000

/*---------------------------------------------------------------------------
  Typedefs
  --------------------------------------------------------------------------*/

/* Parameters for Binary manipulation */
struct tbinparams {
	unsigned int anchor;
	unsigned short ext_anchor;
	unsigned char spi_max_clk;
	unsigned char spi_clk_ratio;
	unsigned char spi_read_mode;
	unsigned char err_detec_cnf;
	unsigned int fw_load_addr;
	unsigned int fw_ep;
	unsigned int fw_err_detec_s_addr;
	unsigned int fw_err_detec_e_addr;
	unsigned int fw_len;
	unsigned int flash_size;
	unsigned int hdr_crc;
	unsigned int fw_crc;
	unsigned int fw_hdr_offset;
	unsigned int bin_params;
} bin_params_struct;

enum verbose_level { NO_VERBOSE = 0, REGULAR_VERBOSE, SUPER_VERBOSE };

enum calc_type { CALC_TYPE_NONE = 0, CALC_TYPE_CHECKSUM, CALC_TYPE_CRC };

struct chip_info {
	unsigned int ram_addr;
	unsigned int ram_size;
} chip_info_struct;

/*------------------------------------------------------------------------*/
/* CRC Variable bit operation macros                                      */
/*------------------------------------------------------------------------*/
#define NUM_OF_BYTES 32
#define READ_VAR_BIT(var, nb) (((var) >> (nb)) & 0x1)
#define SET_VAR_BIT(var, nb, val) ((var) |= ((val) << (nb)))

/*---------------------------------------------------------------------------
  Functions Declaration
  --------------------------------------------------------------------------*/

/* main manipulation */
int main_bin(struct tbinparams binary_parameters);
int main_api(void);
int main_hdr(void);

/* General Checksum\CRC calculation */
void init_calculation(unsigned int *check_sum_crc);
void finalize_calculation(unsigned int *check_sum_crc);
void update_calculation_information(unsigned char crc_con_dat);

/* Checksum calculation etc. (BIN Specific) */
int calc_header_crc_bin(unsigned int *pointer_header_checksum);
int calc_firmware_csum_bin(unsigned int *p_cksum, unsigned int fw_offset,
			   unsigned int fw_length);

/* Checksum calculation etc. (ERP Specific) */
int calc_erp_csum_bin(unsigned short *region_pointer_header_checksum,
		      unsigned int region_pointer_ofs);

/* No words - General */
void exit_with_usage(void);
int copy_file_to_file(char *dst_file_name, char *src_file_name, int offset,
		      int origin);
int write_to_file(unsigned int write_value, unsigned int offset,
		  unsigned char num_of_bytes, char *print_string);
int read_from_file(unsigned int offset, unsigned char size_to_read,
		   unsigned int *read_value, char *print_string);

/* Nice Particular Printf - General */
__attribute__((__format__(__printf__, 2, 3))) void my_printf(int error_level,
							     char *fmt, ...);

int str_cmp_no_case(const char *s1, const char *s2);
int get_file_length(FILE *stream);

#endif /* ECST_H */
