/*
 * Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This utility is used to generate/modify the firmware header which holds
 * data used by NPCX ROM code (booter).
 */

#include "compile_time_macros.h"
#include "ecst.h"

/* Global Variables */
enum verbose_level g_verbose;
char input_file_name[NAME_SIZE];
char output_file_name[NAME_SIZE];
char arg_file_name[NAME_SIZE];
char g_hdr_input_name[NAME_SIZE];
char hdr_args[MAX_ARGS][ARG_SIZE];
char tmp_hdr_args[MAX_ARGS][ARG_SIZE];
FILE *input_file_pointer;
FILE *g_hfd_pointer;
FILE *arg_file_pointer;
FILE *api_file_pointer;
FILE *g_hdr_pointer;
void *gh_console;
unsigned short g_text_atrib;
unsigned short g_bg_atrib;
enum calc_type g_calc_type;
unsigned int ptr_fw_addr;
unsigned int fw_offset;
int is_ptr_merge;
unsigned int g_ram_start_address;
unsigned int g_ram_size;
int api_file_size_bytes;
int is_mrider15 = FALSE;

/* Chips information, RAM start address and RAM size. */
struct chip_info chip_info[] = {
	[NPCX5M5G] = { NPCX5M5G_RAM_ADDR, NPCX5M5G_RAM_SIZE },
	[NPCX5M6G] = { NPCX5M6G_RAM_ADDR, NPCX5M6G_RAM_SIZE },
	[NPCX7M5] = { NPCX7M5X_RAM_ADDR, NPCX7M5X_RAM_SIZE },
	[NPCX7M6] = { NPCX7M6X_RAM_ADDR, NPCX7M6X_RAM_SIZE },
	[NPCX7M7] = { NPCX7M7X_RAM_ADDR, NPCX7M7X_RAM_SIZE },
	[NPCX9M3] = { NPCX9M3X_RAM_ADDR, NPCX9M3X_RAM_SIZE },
	[NPCX9M6] = { NPCX9M6X_RAM_ADDR, NPCX9M6X_RAM_SIZE },
	[NPCX9MFP] = { NPCX9MFP_RAM_ADDR, NPCX9MFP_RAM_SIZE },
};
BUILD_ASSERT(ARRAY_SIZE(chip_info) == NPCX_CHIP_RAM_VAR_NONE);

/* Support chips name strings */
const char *supported_chips =
	"npcx5m5g, npcx5m6g, npcx7m5g, npcx7m6g, "
	"npcx7m6f, npcx7m6fb, npcx7m6fc, npcx7m7fc, npcx7m7wb, "
	"npcx7m7wc, npcx9m3f, npcx9m6f or npcx9mfp";

static unsigned int calc_api_csum_bin(void);
static unsigned int initialize_crc_32(void);
static unsigned int update_crc_32(unsigned int crc, char c);
static unsigned int finalize_crc_32(unsigned int crc);

/*
 * Expects a path in `path`, returning a transformation as follows in `result`
 *
 * The last element of `path` is prefixed with `prefix` if the resulting
 * string fits in an array of `resultsz` characters (incl 0-termination).
 *
 * On success returns TRUE,
 * on error (path too long) prints an error on the TERR channel
 *    and returns FALSE.
 */
static int splice_into_path(char *result, const char *path, int resultsz,
			    const char *prefix)
{
	char *last_delim, *result_last_delim;

	if (strlen(path) + strlen(prefix) + 1 > resultsz) {
		my_printf(TERR,
			  "\n\nfilename '%s' with prefix '%s' too long\n\n",
			  path, prefix);
		my_printf(TINF,
			  "\n\n%zu + %zu + 1 needs to fit in %d bytes\n\n",
			  strlen(path), strlen(prefix), resultsz);
		return FALSE;
	}

	last_delim = strrchr(path, '/');

	if (last_delim == NULL) {
		/* no delimiter: prefix and exit */
		sprintf(result, "%s%s", prefix, path);
		return TRUE;
	}

	/* delimiter: copy, then patch in the prefix */
	strcpy(result, path);
	result_last_delim = result + (last_delim - path);
	sprintf(result_last_delim + 1, "%s%s", prefix, last_delim + 1);
	return TRUE;
}

/**
 * Convert the chip name (string) to the chip's RAM variant.
 * @param    chip_name - the string of the npcx chip variant.
 *
 * @return   one of enum value of npcx_chip_ram_variant,
 *           NPCX_CHIP_RAM_VAR_NONE otherwise.
 */
static enum npcx_chip_ram_variant chip_to_ram_var(const char *chip_name)
{
	if (str_cmp_no_case(chip_name, "npcx9m6f") == 0)
		return NPCX9M6;
	else if (str_cmp_no_case(chip_name, "npcx9m3f") == 0)
		return NPCX9M3;
	else if (str_cmp_no_case(chip_name, "npcx9mfp") == 0)
		return NPCX9MFP;
	else if (str_cmp_no_case(chip_name, "npcx7m7wb") == 0)
		return NPCX7M7;
	else if (str_cmp_no_case(chip_name, "npcx7m7wc") == 0)
		return NPCX7M7;
	else if (str_cmp_no_case(chip_name, "npcx7m7fc") == 0)
		return NPCX7M7;
	else if (str_cmp_no_case(chip_name, "npcx7m6f") == 0)
		return NPCX7M6;
	else if (str_cmp_no_case(chip_name, "npcx7m6fb") == 0)
		return NPCX7M6;
	else if (str_cmp_no_case(chip_name, "npcx7m6fc") == 0)
		return NPCX7M6;
	else if (str_cmp_no_case(chip_name, "npcx7m6g") == 0)
		return NPCX7M6;
	else if (str_cmp_no_case(chip_name, "npcx7m5g") == 0)
		return NPCX7M5;
	else if (str_cmp_no_case(chip_name, "npcx5m6g") == 0)
		return NPCX5M6G;
	else if (str_cmp_no_case(chip_name, "npcx5m5g") == 0)
		return NPCX5M5G;
	else
		return NPCX_CHIP_RAM_VAR_NONE;
}

/*
 *----------------------------------------------------------------------
 * Function:	main()
 * Parameters:	argc, argv
 * Return:		0
 * Description:	Parse the arguments
 *		Chose manipulation routine according to arguments
 *
 *		In case of bin, save optional parameters given by user
 *----------------------------------------------------------------------
 */
int main(int argc, char *argv[])

{
	int mode_choose = FALSE;
	/* Do we get a bin File? */
	int main_fw_hdr_flag = FALSE;
	/* Do we get a API bin File? */
	int main_api_flag = FALSE;
	/* Do we need to create a BootLoader Header file? */
	int main_hdr_flag = FALSE;

	/* Following variables: common to all modes */
	int main_status = TRUE;
	unsigned int main_temp = 0L;
	char main_str_temp[TMP_STR_SIZE];
	char *end_ptr;

	int arg_num;
	int arg_ind;
	int tmp_ind;
	int tmp_arg_num;
	int cur_arg_index;
	FILE *tmp_file;

	/* Following variables are used when bin file is provided */
	struct tbinparams bin_params;

	bin_params.bin_params = 0;

	input_file_name[0] = '\0';
	memset(input_file_name, 0, NAME_SIZE);
	output_file_name[0] = '\0';
	memset(output_file_name, 0, NAME_SIZE);
	arg_file_name[0] = '\0';
	memset(arg_file_name, 0, NAME_SIZE);
	g_hdr_input_name[0] = '\0';
	memset(g_hdr_input_name, 0, NAME_SIZE);

	/* Initialize Global variables */
	g_verbose = NO_VERBOSE;

	g_ram_start_address = chip_info[DEFAULT_CHIP].ram_addr;
	g_ram_size = chip_info[DEFAULT_CHIP].ram_size;

	/* Set default values */
	g_calc_type = CALC_TYPE_NONE;
	bin_params.spi_max_clk = SPI_MAX_CLOCK_DEFAULT;
	bin_params.spi_clk_ratio = 0x00;
	bin_params.spi_read_mode = SPI_READ_MODE_DEFAULT;
	bin_params.fw_load_addr = chip_info[DEFAULT_CHIP].ram_addr;
	bin_params.fw_ep = chip_info[DEFAULT_CHIP].ram_addr;
	bin_params.fw_err_detec_s_addr = FW_CRC_START_ADDR;
	bin_params.fw_err_detec_e_addr = FW_CRC_START_ADDR;
	bin_params.flash_size = FLASH_SIZE_DEFAULT;
	bin_params.fw_hdr_offset = 0;

	ptr_fw_addr = 0x00000000;
	fw_offset = 0x00000000;
	is_ptr_merge = FALSE;

	/* Get Standard Output Handler */

	/* Wrong Number of Arguments ? No problem */
	if (argc < 3)
		exit_with_usage();

	/* Read all arguments to local array. */
	for (arg_num = 0; arg_num < argc; arg_num++)
		strncpy(hdr_args[arg_num], argv[arg_num], ARG_SIZE);

	/* Loop all arguments. */
	/* Parse the Arguments - supports ecrp and bin */
	for (arg_ind = 1; arg_ind < arg_num; arg_ind++) {
		/* -h display help screen */
		if (str_cmp_no_case(hdr_args[arg_ind], "-h") == 0)
			exit_with_usage();

		/* -v verbose */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-v") == 0)
			g_verbose = REGULAR_VERBOSE;

		/* Super verbose. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-vv") == 0)
			g_verbose = SUPER_VERBOSE;

		else if (str_cmp_no_case(hdr_args[arg_ind], "-mode") == 0) {
			mode_choose = TRUE;
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%s", main_str_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read operation mode");
				my_printf(TERR, ", bt, bh or api. !\n");
				main_status = FALSE;
			} else {
				/* bt, bh and api should not coexist */
				if (main_fw_hdr_flag || main_api_flag ||
				    main_hdr_flag) {
					my_printf(TERR, "\nOperation modes bt");
					my_printf(TERR, ", bh, and api should");
					my_printf(TERR, " not coexist.\n");
					main_status = FALSE;
				}

				if (str_cmp_no_case(main_str_temp, "bt") == 0)
					main_fw_hdr_flag = TRUE;
				else if (str_cmp_no_case(main_str_temp, "bh") ==
					 0)
					main_hdr_flag = TRUE;
				else if (str_cmp_no_case(main_str_temp,
							 "api") == 0)
					main_api_flag = TRUE;
				else {
					my_printf(TERR,
						  "\nInvalid operation mode ");
					my_printf(TERR, "(%s)\n",
						  main_str_temp);
					main_status = FALSE;
				}
			}
		}

		else if (str_cmp_no_case(hdr_args[arg_ind], "-chip") == 0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%s", main_str_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read chip name %s.\n",
					  supported_chips);
				main_status = FALSE;
			} else {
				enum npcx_chip_ram_variant ram_variant;

				ram_variant = chip_to_ram_var(main_str_temp);
				if (ram_variant == NPCX_CHIP_RAM_VAR_NONE) {
					my_printf(TERR,
						  "\nInvalid chip name (%s) ",
						  main_str_temp);
					my_printf(TERR, ", it should be %s.\n",
						  supported_chips);
					main_status = FALSE;
					break;
				}

				if ((bin_params.bin_params &
				     BIN_FW_LOAD_START_ADDR) == 0x00000000)
					bin_params.fw_load_addr =
						chip_info[ram_variant].ram_addr;

				if ((bin_params.bin_params &
				     BIN_FW_ENTRY_POINT) == 0x00000000)
					bin_params.fw_ep =
						chip_info[ram_variant].ram_addr;

				g_ram_start_address =
					chip_info[ram_variant].ram_addr;
				g_ram_size = chip_info[ram_variant].ram_size;

				if ((ram_variant == NPCX5M5G) ||
				    (ram_variant == NPCX5M6G)) {
					is_mrider15 = TRUE;
				}
			}
			/* -argfile Read argument file. File name must be after
			 * it.*/
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-argfile") ==
			   0) {
			arg_ind++;
			if (arg_ind < arg_num) {
				strncpy(arg_file_name, hdr_args[arg_ind],
					sizeof(arg_file_name) - 1);
				arg_file_pointer = fopen(arg_file_name, "rt");
				if (arg_file_pointer == NULL) {
					my_printf(TERR,
						  "\n\nCannot open %s\n\n",
						  arg_file_name);
					main_status = FALSE;
				} else {
					cur_arg_index = arg_ind;

					/* Copy the arguments to temp array. */
					for (tmp_ind = 0;
					     (tmp_ind + arg_ind + 1) < arg_num;
					     tmp_ind++)
						strncpy(tmp_hdr_args[tmp_ind],
							hdr_args[tmp_ind +
								 arg_ind + 1],
							ARG_SIZE);

					tmp_arg_num = tmp_ind;

					/* Read arguments from file to array */
					for (arg_ind++;
					     fscanf(arg_file_pointer, "%s",
						    hdr_args[arg_ind]) == 1;
					     arg_ind++)
						;

					fclose(arg_file_pointer);
					arg_file_pointer = NULL;

					/* Copy back the restored arguments. */
					for (tmp_ind = 0;
					     (tmp_ind < tmp_arg_num) &&
					     (arg_ind < MAX_ARGS);
					     tmp_ind++) {
						strncpy(hdr_args[arg_ind++],
							tmp_hdr_args[tmp_ind],
							ARG_SIZE);
					}
					arg_num = arg_ind;
					arg_ind = cur_arg_index;
				}

			} else {
				my_printf(TERR,
					  "\nMissing Argument File Name\n");
				main_status = FALSE;
			}
			/* -i get input file name. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-i") == 0) {
			arg_ind++;
			if (arg_ind < arg_num) {
				strncpy(input_file_name, hdr_args[arg_ind],
					sizeof(input_file_name) - 1);
			} else {
				my_printf(TERR, "\nMissing Input File Name\n");
				main_status = FALSE;
			}
			/* -o Get output file name. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-o") == 0) {
			arg_ind++;
			if (arg_ind < arg_num) {
				strncpy(output_file_name, hdr_args[arg_ind],
					sizeof(output_file_name) - 1);
			} else {
				my_printf(TERR,
					  "\nMissing Output File Name.\n");
				main_status = FALSE;
			}
			/* -usearmrst get FW entry point from FW image
			 * offset 4.*/
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-usearmrst") ==
			   0) {
			if ((bin_params.bin_params & BIN_FW_ENTRY_POINT) !=
			    0x00000000) {
				my_printf(TERR, "\n-usearmrst not allowed, ");
				my_printf(TERR, "FW entry point already set ");
				my_printf(TERR, "using -fwep !\n");
				main_status = FALSE;
			} else
				bin_params.bin_params |= BIN_FW_USER_ARM_RESET;
			/* -nohcrs disable header CRC*/
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-nohcrc") == 0)
			bin_params.bin_params |= BIN_FW_HDR_CRC_DISABLE;
		/* -ph merg header in BIN file. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-ph") == 0) {
			bin_params.bin_params |= BIN_FW_HDR_OFFSET;
			if ((strlen(hdr_args[arg_ind + 1]) == 0) ||
			    (sscanf(hdr_args[arg_ind + 1], "%x", &main_temp) !=
			     1))
				bin_params.fw_hdr_offset = 0;
			else {
				arg_ind++;
				bin_params.fw_hdr_offset = main_temp;
			}
			/* -spimaxclk  Get SPI flash max clock. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-spimaxclk") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%d", &main_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read SPI Flash Max");
				my_printf(TERR, " Clock !\n");
				main_status = FALSE;
			} else
				bin_params.spi_max_clk =
					(unsigned char)main_temp;
			/* -spiclkratio  Get SPI flash max clock ratio. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-spiclkratio") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%d", &main_temp) !=
			     1)) {
				my_printf(TERR,
					  "\nCannot read SPI Clock Ratio\n");
				main_status = FALSE;
			} else
				bin_params.spi_clk_ratio =
					(unsigned char)main_temp;

			/* spireadmode	get SPI read mode. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-spireadmode") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%20s", main_str_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read SPI Flash");
				my_printf(TERR, " Read Mode !\n");
				main_status = FALSE;
			} else {
				if (str_cmp_no_case(main_str_temp,
						    SPI_NORMAL_MODE_VAL) == 0)
					bin_params.spi_read_mode =
						(unsigned char)SPI_NORMAL_MODE;
				else if (str_cmp_no_case(main_str_temp,
							 SPI_SINGLE_MODE_VAL) ==
					 0)
					bin_params.spi_read_mode =
						(unsigned char)SPI_SINGLE_MODE;
				else if (str_cmp_no_case(main_str_temp,
							 SPI_DUAL_MODE_VAL) ==
					 0)
					bin_params.spi_read_mode =
						(unsigned char)SPI_DUAL_MODE;
				else if (str_cmp_no_case(main_str_temp,
							 SPI_QUAD_MODE_VAL) ==
					 0)
					bin_params.spi_read_mode =
						(unsigned char)SPI_QUAD_MODE;
				else {
					my_printf(TERR,
						  "\nInvalid SPI Flash Read ");
					my_printf(TERR,
						  "Mode (%s), it should be ",
						  main_str_temp);
					my_printf(TERR, "normal, singleMode, ");
					my_printf(TERR,
						  "dualMode or quadMode !\n");
					main_status = FALSE;
				}
			}

		}
		/* -unlimburst enable unlimited burst */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-unlimburst") == 0)
			bin_params.bin_params |= BIN_UNLIM_BURST_ENABLE;
		/* -nofcrc disable FW CRC. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-nofcrc") == 0)
			bin_params.bin_params |= BIN_FW_CRC_DISABLE;

		/* -fwloadaddr,  Get the FW load address. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-fwloadaddr") ==
			 0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%x", &main_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read FW Load ");
				my_printf(TERR, "\nstart address !\n");
				main_status = FALSE;
			} else {
				/* Check that the address is 16-bytes aligned */
				if ((main_temp & ADDR_16_BYTES_ALIGNED_MASK) !=
				    0) {
					my_printf(TERR,
						  "\nFW load address start ");
					my_printf(TERR,
						  "address (0x%08X) is not ",
						  main_temp);
					my_printf(TERR, "16-bytes aligned !\n");
					main_status = FALSE;
				} else {
					bin_params.fw_load_addr = main_temp;
					bin_params.bin_params |=
						BIN_FW_LOAD_START_ADDR;
				}
			}
			/* -fwep, Get the FW entry point. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-fwep") == 0) {
			if ((bin_params.bin_params & BIN_FW_USER_ARM_RESET) !=
			    0x00000000) {
				my_printf(
					TERR,
					"\n-fwep not allowed, FW entry point");
				my_printf(TERR,
					  " already set using -usearmrst!\n");
				main_status = FALSE;
			} else {
				arg_ind++;
				if ((hdr_args[arg_ind] == NULL) ||
				    (sscanf(hdr_args[arg_ind], "%x",
					    &main_temp) != 1)) {
					my_printf(TERR,
						  "\nCan't read FW E-Point\n");
					main_status = FALSE;
				} else {
					bin_params.fw_ep = main_temp;
					bin_params.bin_params |=
						BIN_FW_ENTRY_POINT;
				}
			}
			/*
			 * -crcstart, Get the address from where to calculate
			 * the FW CRC.
			 */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-crcstart") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%x", &main_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read FW CRC");
				my_printf(TERR, " start address !\n");
				main_status = FALSE;
			} else {
				bin_params.fw_err_detec_e_addr =
					bin_params.fw_err_detec_e_addr -
					bin_params.fw_err_detec_s_addr +
					main_temp;
				bin_params.fw_err_detec_s_addr = main_temp;
				bin_params.bin_params |= BIN_FW_CKS_START;
			}
			/* -crcsize,  Get the area size that need to be CRCed.
			 */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-crcsize") ==
			   0) {
			arg_ind++;
			main_temp = 0x00;
			if (hdr_args[arg_ind] == NULL)
				end_ptr = NULL;
			else
				main_temp =
					strtol(hdr_args[arg_ind], &end_ptr, 16);

			if (hdr_args[arg_ind] == end_ptr) {
				my_printf(TERR,
					  "\nCannot read FW CRC area size !\n");
				main_status = FALSE;
			} else {
				bin_params.fw_err_detec_e_addr =
					bin_params.fw_err_detec_s_addr +
					main_temp - 1;
				bin_params.bin_params |= BIN_FW_CKS_SIZE;
			}
		}
		/* -fwlen, Get the FW length. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-fwlen") == 0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%x", &main_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read FW length !\n");
				main_status = FALSE;
			} else {
				bin_params.fw_len = main_temp;
				bin_params.bin_params |= BIN_FW_LENGTH;
			}
		}
		/* flashsize, Get the flash size. */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-flashsize") ==
			 0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%d", &main_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read Flash size !\n");
				main_status = FALSE;
			} else
				bin_params.flash_size = main_temp;
			/* -apisign, Get the method for error detect
			 * calculation. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-apisign") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%s", main_str_temp) !=
			     1)) {
				my_printf(TERR, "\nCannot read API sign, CRC,");
				my_printf(TERR, " CheckSum or None. !\n");
				main_status = FALSE;
			} else {
				if (!main_api_flag) {
					my_printf(TERR, "\n-apisign is valid ");
					my_printf(TERR, "-only with -api.\n");
					main_status = FALSE;
				}

				if (str_cmp_no_case(main_str_temp, "crc") == 0)
					g_calc_type = CALC_TYPE_CRC;

				else if (str_cmp_no_case(main_str_temp,
							 "checksum") == 0)
					g_calc_type = CALC_TYPE_CHECKSUM;

				else {
					my_printf(TERR,
						  "\nInvalid API sign (%s)\n",
						  main_str_temp);
					main_status = FALSE;
				}
			}
			/* -pointer,  Get the FW image address. */
		} else if (str_cmp_no_case(hdr_args[arg_ind], "-pointer") ==
			   0) {
			arg_ind++;
			if ((hdr_args[arg_ind] == NULL) ||
			    (sscanf(hdr_args[arg_ind], "%x", &main_temp) !=
			     1)) {
				my_printf(TERR,
					  "\nCannot read FW Image address !\n");
				main_status = FALSE;
			} else {
				/* Check that the address is 16-bytes aligned */
				if ((main_temp & ADDR_16_BYTES_ALIGNED_MASK) !=
				    0) {
					my_printf(TERR,
						  "\nFW Image address (0x%08X)"
						  " isn't 16-bytes aligned !\n",
						  main_temp);
					main_status = FALSE;
				}

				if (main_temp > MAX_FLASH_SIZE) {
					my_printf(TERR,
						  "\nPointer address (0x%08X) ",
						  main_temp);
					my_printf(TERR,
						  "is higher from flash size");
					my_printf(TERR, " (0x%08X) !\n",
						  MAX_FLASH_SIZE);
					main_status = FALSE;
				} else {
					ptr_fw_addr = main_temp;
					is_ptr_merge = FALSE;
				}
			}
		}
		/* -bhoffset,  BootLoader Header Offset (BH location in BT). */
		else if (str_cmp_no_case(hdr_args[arg_ind], "-bhoffset") == 0) {
			arg_ind++;
			main_temp = 0x00;
			if (hdr_args[arg_ind] == NULL)
				end_ptr = NULL;
			else
				main_temp =
					strtol(hdr_args[arg_ind], &end_ptr, 16);

			if (hdr_args[arg_ind] == end_ptr) {
				my_printf(TERR, "\nCannot read BootLoader");
				my_printf(TERR, " Header Offset !\n");
				main_status = FALSE;
			} else {
				/* Check that the address is 16-bytes aligned */
				if ((main_temp & ADDR_16_BYTES_ALIGNED_MASK) !=
				    0) {
					my_printf(
						TERR,
						"\nFW Image address (0x%08X) ",
						main_temp);
					my_printf(TERR,
						  "is not 16-bytes aligned!\n");
				}

				if (main_temp > MAX_FLASH_SIZE) {
					my_printf(TERR,
						  "\nFW Image address (0x%08X)",
						  main_temp);
					my_printf(TERR,
						  " is higher from flash size");
					my_printf(TERR, " (0x%08X) !\n",
						  MAX_FLASH_SIZE);
					main_status = FALSE;
				} else {
					fw_offset = main_temp;
					is_ptr_merge = TRUE;
				}
			}
		} else {
			my_printf(TERR, "\nUnknown flag: %s\n",
				  hdr_args[arg_ind]);
			main_status = FALSE;
		}
	}

	/*
	 * If the input and output file have the same name then exit with error.
	 */
	if (strcmp(output_file_name, input_file_name) == 0) {
		my_printf(TINF,
			  "Input file name (%s) should be differed from\n",
			  input_file_name);
		my_printf(TINF, "Output file name (%s).\n", output_file_name);
		main_status = FALSE;
	}

	/* No problems reading argv? So go on... */
	if (main_status) {
		/* if output file already exist, then delete it. */
		tmp_file = fopen(output_file_name, "w");
		if (tmp_file != NULL)
			fclose(tmp_file);

		/* If no mode choose than "bt" is the default mode.*/
		if (mode_choose == FALSE)
			main_fw_hdr_flag = TRUE;

		/* Chose manipulation routine according to arguments */
		if (main_fw_hdr_flag)
			main_status = main_bin(bin_params);
		else if (main_api_flag)
			main_status = main_api();
		else if (main_hdr_flag)
			main_status = main_hdr();
		else
			exit_with_usage();
	}

	/* Be sure there's no open file before you leave */
	if (input_file_pointer)
		fclose(input_file_pointer);
	if (g_hfd_pointer)
		fclose(g_hfd_pointer);
	if (api_file_pointer)
		fclose(api_file_pointer);

	/* Delete temprary header file. */
	remove(g_hdr_input_name);

	/* Say Bye Bye */
	if (main_status) {
		my_printf(TPAS, "\n\n******************************");
		my_printf(TPAS, "\n***    SUCCESS     ***");
		my_printf(TPAS, "\n******************************\n");

		exit(EXIT_SUCCESS);
	} else {
		my_printf(TERR, "\n\n******************************");
		my_printf(TERR, "\n***    FAILED      ***");
		my_printf(TERR, "\n******************************\n");

		exit(EXIT_FAILURE);
	}
}

/*
 *-----------------------------------------------------------------------
 * Function:	exit_with_usage()
 * Parameters:	none
 * Return:		none
 * Description: No Words...
 *-----------------------------------------------------------------------
 */
void exit_with_usage(void)
{
	my_printf(TUSG,
		  "\nECST, Embedded Controller Sign Tool, version %d.%d.%d",
		  T_VER, T_REV_MAJOR, T_REV_MINOR);
	my_printf(TUSG, "\n");
	my_printf(TUSG, "\nUsage:");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\n ECST -mode <bt|bh|api> -i <filename> [Flags]");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\nOperation Modes: ");
	my_printf(TUSG, "\n bt  - BootLoader Table");
	my_printf(TUSG, "\n bh  - BootLoader Header");
	my_printf(TUSG, "\n api - Download from Flash API");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\nCommon flags:");
	my_printf(TUSG, "\n -mode <type>        - Operation mode: ");
	my_printf(TUSG, "bt|bh|api (default is bt)");
	my_printf(TUSG, "\n -i <filename>       - Input file name; ");
	my_printf(TUSG, "must differ from the output file name");
	my_printf(TUSG, "\n -o <filename>       - Output file name ");
	my_printf(TUSG, "(default is out_<input_filename>.bin)");
	my_printf(TUSG, "\n -argfile <filename> - Arguments file name; ");
	my_printf(TUSG, "includes multiple flags");
	my_printf(TUSG, "\n -chip <name>        - Supported EC Chip Name: ");
	my_printf(TUSG, "%s. ", supported_chips);
	my_printf(TUSG, "(default is npcx5m5g)");
	my_printf(TUSG, "\n -v          - Verbose; prints ");
	my_printf(TUSG, "information messages");
	my_printf(TUSG, "\n -vv         - Super Verbose; prints ");
	my_printf(TUSG, "intermediate calculations");
	my_printf(TUSG, "\n -h          - Show this help screen");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\nBootLoader Table mode flags:");
	my_printf(TUSG, "\n -nohcrc     - Disable CRC on header ");
	my_printf(TUSG, "(default is ON)");
	my_printf(TUSG, "\n -nofcrc     - Disable CRC on firmware ");
	my_printf(TUSG, "(default is ON)");
	my_printf(TUSG, "\n -spimaxclk <val>    - SPI Flash Maximum Clock, in");
	my_printf(TUSG, " MHz: 20|25|33|40|50 (default is 20)");
	my_printf(TUSG, "\n -spiclkratio <val>  - Core Clock / SPI Flash ");
	my_printf(TUSG, "Clocks Ratio: 1 | 2 (default is 1)");
	my_printf(TUSG, "\n                       ");
	my_printf(TUSG, "Note: Not relevant for npcx5mng chips family");
	my_printf(TUSG, "\n -spireadmode <type> - SPI Flash Read Mode: ");
	my_printf(TUSG, "normal|fast|dual|quad (default is normal)");
	my_printf(TUSG, "\n -unlimburst         - Enable FIU Unlimited ");
	my_printf(TUSG, "\n                       ");
	my_printf(TUSG, "Note: Not relevant for npcx5mng chips family");
	my_printf(TUSG, "Burst for SPI Flash Accesses (default is disable).");
	my_printf(TUSG, "\n -fwloadaddr <addr>  - Firmware load start ");
	my_printf(TUSG, "address (default is Start-of-RAM)");
	my_printf(TUSG, "\n           Located in code RAM, ");
	my_printf(TUSG, "16-bytes aligned, hex format");
	my_printf(TUSG, "\n -usearmrst      - Use the ARM reset table ");
	my_printf(TUSG, "entry as the Firmware Entry Point");
	my_printf(TUSG, "\n           Can't be used with -fwep");
	my_printf(TUSG, "\n -fwep <addr>        - Firmware entry ");
	my_printf(TUSG, "point (default is Firmware Entry Point)");
	my_printf(TUSG, "\n           Located in firmware area,");
	my_printf(TUSG, " hex format");
	my_printf(TUSG, "\n -crcstart <offset>  - Firmware CRC start offset ");
	my_printf(TUSG, "(default is 00000000)");
	my_printf(TUSG, "\n           Offset from firmware image,");
	my_printf(TUSG, " 4B-aligned, for partial CRC, hex format");
	my_printf(TUSG, "\n -crcsize <val>      - Firmware CRC size ");
	my_printf(TUSG, "(default is entire firmware size)");
	my_printf(TUSG, "\n           4B-aligned, for partial ");
	my_printf(TUSG, "CRC, hex format");
	my_printf(TUSG, "\n -fwlen <val>        - Firmware length, ");
	my_printf(TUSG, "16B-aligned, hex format (default is file size).");
	my_printf(TUSG, "\n -flashsize <val>    - Flash size, in MB: ");
	my_printf(TUSG, "1|2|4|8|16 (default is 16)");
	my_printf(TUSG, "\n -ph <offset>        - Paste the Firmware ");
	my_printf(TUSG, "Header in the input file copy at the selected");
	my_printf(TUSG, "\n           offset ");
	my_printf(TUSG, "(default is 00000000), hex format.");
	my_printf(TUSG, "\n           The firmware itself is ");
	my_printf(TUSG, "expected to start at offset + 64 bytes.");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\nBootLoader Header mode flags:");
	my_printf(TUSG, "\n -pointer <offset>   - BootLoader Table location");
	my_printf(TUSG, " in the flash, hex format");
	my_printf(TUSG, "\n -bhoffset <offset>  - BootLoader Header Offset");
	my_printf(TUSG, " in file, hex format (BH location in BT)");
	my_printf(TUSG, "\n ");
	my_printf(TUSG, "\nAPI mode flags:");
	my_printf(TUSG, "\n -apisign <type> - Signature type: ");
	my_printf(TUSG, "crc|checksum (default is OFF)");
	my_printf(TUSG, "\n\n");

	exit(EXIT_FAILURE);
}

/*
 *--------------------------------------------------------------------------
 * Function:	copy_file_to_file()
 * Parameters:	dst_file_name	- Destination file name.
 *		src_file_name	- Source file name.
 *		offset		- number of bytes from the origin.
 *		origin		- From where to seek, START, END, or CURRENT of
 *				  file.
 * Return: Number of copied bytes
 * Description: Copy the source file to the end of the destination file.
 *--------------------------------------------------------------------------
 */
int copy_file_to_file(char *dst_file_name, char *src_file_name, int offset,
		      int origin)
{
	int index = 0;
	int result = 0;
	unsigned char local_val;
	int src_file_size;
	FILE *dst_file;
	FILE *src_file;

	/* Open the destination file for append. */
	dst_file = fopen(dst_file_name, "r+b");
	if (dst_file == NULL) {
		/* destination file not exist, create it. */
		dst_file = fopen(dst_file_name, "ab");
		if (dst_file == NULL)
			return 0;
	}

	/* Open the source file for read. */
	src_file = fopen(src_file_name, "rb");
	if (src_file == NULL) {
		fclose(dst_file);
		return 0;
	}

	/* Get the source file length in bytes. */
	src_file_size = get_file_length(src_file);

	/* Point to the end of the destination file, and to the start */
	/* of the source file. */
	if (fseek(dst_file, offset, origin) < 0)
		goto out;
	if (fseek(src_file, 0, SEEK_SET) < 0)
		goto out;

	/* Loop over all destination file and write it to the source file.*/
	for (index = 0; index < src_file_size; index++) {
		/* Read byte from source file. */
		result = (int)(fread(&local_val, 1, 1, src_file));

		/* If byte reading pass than write it to the destination, */
		/* else exit from the reading loop. */
		if (result) {
			/* Read pass, so write it to destination file.*/
			result = fwrite(&local_val, 1, 1, dst_file);
			if (!result)
				/*
				 * Write failed,
				 * return with the copied bytes number.
				 */
				break;
		} else
			/* Read failed, return with the copied bytes number. */
			break;
	}

out:
	/* Close the files. */
	fclose(dst_file);
	fclose(src_file);

	/* Copy ended, return with the number of bytes that were copied. */
	return index;
}

/*
 *--------------------------------------------------------------------------
 * Function:	my_printf()
 * Parameters:	as in printf + error level
 * Return:		none
 * Description: No Words...
 *--------------------------------------------------------------------------
 */
void my_printf(int error_level, char *fmt, ...)
{
	va_list argptr;

	if ((g_verbose == NO_VERBOSE) && (error_level == TINF))
		return;

	if ((g_verbose != SUPER_VERBOSE) && (error_level == TDBG))
		return;

	va_start(argptr, fmt);
	vprintf(fmt, argptr);
	va_end(argptr);
}

/*
 *--------------------------------------------------------------------------
 * Function:	 write_to_file
 * Parameters:	 TBD
 * Return:	 TRUE on successful write
 * Description:	 Writes to ELF or BIN files - whatever is open
 *--------------------------------------------------------------------------
 */
int write_to_file(unsigned int write_value, unsigned int offset,
		  unsigned char num_of_bytes, char *print_string)
{
	int result = 0;
	int index;
	unsigned int localValue4;
	unsigned short localValue2;
	unsigned char localValue1;

	if (fseek(g_hfd_pointer, offset, SEEK_SET) < 0)
		return FALSE;

	switch (num_of_bytes) {
	case (1):
		localValue1 = (unsigned char)write_value;
		result = (int)(fwrite(&localValue1, 1, 1, g_hfd_pointer));
		break;
	case (2):
		localValue2 = (unsigned short)write_value;
		result = (int)(fwrite(&localValue2, 2, 1, g_hfd_pointer));
		break;
	case (4):
		localValue4 = write_value;
		result = (int)(fwrite(&localValue4, 4, 1, g_hfd_pointer));
		break;
	default:
		/* Pad the same value N times. */
		localValue1 = (unsigned char)write_value;
		for (index = 0; index < num_of_bytes; index++)
			result = (int)(fwrite(&localValue1, 1, 1,
					      g_hfd_pointer));
		break;
	}

	my_printf(TINF, "\nIn write_to_file  - %s", print_string);

	if (result) {
		my_printf(TINF, " - Offset %2d - value 0x%x", offset,
			  write_value);
	} else {
		my_printf(TERR, "\n\nCouldn't write %x to file at %x\n\n",
			  write_value, offset);
		return FALSE;
	}

	return TRUE;
}

/*
 *--------------------------------------------------------------------------
 * Function:	 read_from_file
 * Parameters:	 TBD
 * Return:       TRUE on successful read
 * Description : Reads from open BIN file
 *--------------------------------------------------------------------------
 */
int read_from_file(unsigned int offset, unsigned char size_to_read,
		   unsigned int *read_value, char *print_string)
{
	int result;
	unsigned int localValue4;
	unsigned short localValue2;
	unsigned char localValue1;

	if (fseek(input_file_pointer, offset, SEEK_SET) < 0)
		return FALSE;

	switch (size_to_read) {
	case (1):
		result = (int)(fread(&localValue1, 1, 1, input_file_pointer));
		*read_value = localValue1;
		break;
	case (2):
		result = (int)(fread(&localValue2, 2, 1, input_file_pointer));
		*read_value = localValue2;
		break;
	case (4):
		result = (int)(fread(&localValue4, 4, 1, input_file_pointer));
		*read_value = localValue4;
		break;
	default:
		my_printf(TERR, "\nIn read_from_file - %s", print_string);
		my_printf(TERR, "\n\nInvalid call to read_from_file\n\n");
		return FALSE;
	}

	my_printf(TINF, "\nIn read_from_file - %s", print_string);

	if (result) {
		my_printf(TINF, " - Offset %d - value %x", offset, *read_value);
	} else {
		my_printf(TERR, "\n\nCouldn't read from file at %x\n\n",
			  offset);
		return FALSE;
	}

	return TRUE;
}

/*
 *--------------------------------------------------------------------------
 * Function:	init_calculation
 * Parameters:	unsigned int check_sum_crc (I\O)
 * Return:
 * Description:	Initialize the variable according to the selected
 *				calculation
 *--------------------------------------------------------------------------
 */
void init_calculation(unsigned int *check_sum_crc)
{
	switch (g_calc_type) {
	case CALC_TYPE_NONE:
	case CALC_TYPE_CHECKSUM:
		*check_sum_crc = 0;
		break;
	case CALC_TYPE_CRC:
		*check_sum_crc = initialize_crc_32();
		break;
	}
}

/*
 *--------------------------------------------------------------------------
 * Function:	 finalize_calculation
 * Parameters:	 unsigned int check_sum_crc (I\O)
 * Return:
 * Description:	Finalize the variable according to the selected calculation
 *--------------------------------------------------------------------------
 */
void finalize_calculation(unsigned int *check_sum_crc)
{
	switch (g_calc_type) {
	case CALC_TYPE_NONE:
	case CALC_TYPE_CHECKSUM:
		/* Do nothing */
		break;
	case CALC_TYPE_CRC:
		*check_sum_crc = finalize_crc_32(*check_sum_crc);
		break;
	}
}

/*--------------------------------------------------------------------------
 * Function:	 update_calculation
 * Parameters:	 unsigned int check_sum_crc (I\O)
 *		 unsigned int byte_to_add (I)
 * Return:
 * Description:	 Calculate a new checksum\crc with the new byte_to_add
 *		 given the previous checksum\crc
 *--------------------------------------------------------------------------
 */
void update_calculation(unsigned int *check_sum_crc, unsigned char byte_to_add)
{
	switch (g_calc_type) {
	case CALC_TYPE_NONE:
		/* Do nothing */
		break;
	case CALC_TYPE_CHECKSUM:
		*check_sum_crc += byte_to_add;
		break;
	case CALC_TYPE_CRC:
		*check_sum_crc = update_crc_32(*check_sum_crc, byte_to_add);
		break;
	}
}

/*
 *--------------------------------------------------------------------------
 * Function:	 str_cmp_no_case
 * Parameters:	 s1, s2: Strings to compare.
 * Return:	 function returns an integer less than, equal to, or
 *		 greater than zero if s1 (or the first n bytes thereof) is
 *		 found, respectively, to be less than, to match, or be
 *		 greater than s2.
 * Description:	 Compare two string without case sensitive.
 *--------------------------------------------------------------------------
 */
int str_cmp_no_case(const char *s1, const char *s2)
{
	return strcasecmp(s1, s2);
}

/*
 *--------------------------------------------------------------------------
 * Function:	 get_file_length
 * Parameters:	 stream - Pointer to a FILE object
 * Return:		 File length in bytes or -1 on error
 * Description:	 Gets the file length in bytes.
 *--------------------------------------------------------------------------
 */
int get_file_length(FILE *stream)
{
	int current_position;
	int file_len;

	/* Store current position. */
	current_position = ftell(stream);
	if (current_position < 0)
		return -1;

	/* End position of the file is its length. */
	if (fseek(stream, 0, SEEK_END) < 0)
		return -1;
	file_len = ftell(stream);

	/* Restore the original position. */
	if (fseek(stream, current_position, SEEK_SET) < 0)
		return -1;

	/* return file length. */
	return file_len;
}

/*
 ***************************************************************************
 *							"bt" mode Handler
 ***************************************************************************
 */

/*
 ***************************************************************************
 * Function:	 main_bin
 * Parameters:	 TBD
 * Return:		 True for success
 * Description:
 *	 TBD
 ***************************************************************************
 */
int main_bin(struct tbinparams binary_params)
{
	unsigned int bin_file_size_bytes;
	unsigned int bin_fw_offset = 0;
	unsigned int tmp_param;
	FILE *output_file_pointer;

	/* If input file was not declared, then print error message. */
	if (strlen(input_file_name) == 0) {
		my_printf(TERR, "\n\nDefine input file, using -i flag\n\n");
		return FALSE;
	}

	/* Open input file */
	input_file_pointer = fopen(input_file_name, "r+b");
	if (input_file_pointer == NULL) {
		my_printf(TERR, "\n\nCannot open %s\n\n", input_file_name);
		return FALSE;
	}

	/*
	 * Check Binary file size, this file contain the image itself,
	 * without any header.
	 */
	bin_file_size_bytes = get_file_length(input_file_pointer);
	if (bin_file_size_bytes == 0) {
		my_printf(TINF,
			  "\nBIN Input file name %s is empty (size is %d)\n",
			  input_file_name, bin_file_size_bytes);
		return FALSE;
	}

	/*
	 * If the binary file contains also place for the header, then the FW
	 * size is the length of the file minus the header length
	 */
	if ((binary_params.bin_params & BIN_FW_HDR_OFFSET) != 0)
		bin_fw_offset = binary_params.fw_hdr_offset + HEADER_SIZE;

	my_printf(TINF, "\nBIN file:  %s, size: %d (0x%x) bytes\n",
		  input_file_name, bin_file_size_bytes, bin_file_size_bytes);

	/* Check validity of FW header offset. */
	if (((int)binary_params.fw_hdr_offset < 0) ||
	    (binary_params.fw_hdr_offset > bin_file_size_bytes)) {
		my_printf(TERR,
			  "\nFW header offset 0x%08x (%d) should be in the"
			  " range of 0 and file size (%d).\n",
			  binary_params.fw_hdr_offset,
			  binary_params.fw_hdr_offset, bin_file_size_bytes);
		return FALSE;
	}

	/* Create the header file in the same directory as the input file. */
	if (!splice_into_path(g_hdr_input_name, input_file_name,
			      sizeof(g_hdr_input_name), "hdr_"))
		return FALSE;
	g_hfd_pointer = fopen(g_hdr_input_name, "w+b");
	if (g_hfd_pointer == NULL) {
		my_printf(TERR, "\n\nCannot open %s\n\n", g_hdr_input_name);
		return FALSE;
	}

	if (strlen(output_file_name) == 0) {
		if (!splice_into_path(output_file_name, input_file_name,
				      sizeof(output_file_name), "out_"))
			return FALSE;
	}

	my_printf(TINF, "Output file name: %s\n", output_file_name);

	/*
	 *********************************************************************
	 * Set the ANCHOR & Extended-ANCHOR
	 *********************************************************************
	 */
	/* Write the ancore. */
	if (!write_to_file(FW_HDR_ANCHOR, HDR_ANCHOR_OFFSET, 4,
			   "HDR - FW Header ANCHOR		  "))
		return FALSE;

	/* Write the extended anchor. */
	if (binary_params.bin_params & BIN_FW_HDR_CRC_DISABLE) {
		/* Write the ancore and the extended anchor. */
		if (!write_to_file(FW_HDR_EXT_ANCHOR_DISABLE,
				   HDR_EXTENDED_ANCHOR_OFFSET, 2,
				   "HDR - Header EXTENDED ANCHOR "))
			return FALSE;
	} else {
		/* Write the anchor and the extended anchor. */
		if (!write_to_file(FW_HDR_EXT_ANCHOR_ENABLE,
				   HDR_EXTENDED_ANCHOR_OFFSET, 2,
				   "HDR - Header EXTENDED ANCHOR "))
			return FALSE;
	}

	/* Write the SPI flash MAX clock. */
	switch (binary_params.spi_max_clk) {
	case SPI_MAX_CLOCK_20_MHZ_VAL:
		tmp_param = SPI_MAX_CLOCK_20_MHZ;
		break;
	case SPI_MAX_CLOCK_25_MHZ_VAL:
		tmp_param = SPI_MAX_CLOCK_25_MHZ;
		break;
	case SPI_MAX_CLOCK_33_MHZ_VAL:
		tmp_param = SPI_MAX_CLOCK_33_MHZ;
		break;
	case SPI_MAX_CLOCK_40_MHZ_VAL:
		tmp_param = SPI_MAX_CLOCK_40_MHZ;
		break;
	case SPI_MAX_CLOCK_50_MHZ_VAL:
		tmp_param = SPI_MAX_CLOCK_50_MHZ;
		break;
	default:
		my_printf(TERR, "\n\nInvalid SPI Flash MAX clock (%d MHz) ",
			  binary_params.spi_max_clk);
		my_printf(TERR, "- it should be 20, 25, 33, 40 or 50 MHz");
		return FALSE;
	}

	/* If SPI clock ratio set for MRIDER15, then it is error. */
	if ((binary_params.spi_clk_ratio != 0x00) && (is_mrider15 == TRUE)) {
		my_printf(TERR, "\nspiclkratio is not relevant for");
		my_printf(TERR, " npcx5mng chips family !\n");

		return FALSE;
	}

	/*
	 * In case SPIU clock ratio didn't set by the user,
	 *  set it to its default value.
	 */
	if (binary_params.spi_clk_ratio == 0x00)
		binary_params.spi_clk_ratio = SPI_CLOCK_RATIO_1_VAL;

	switch (binary_params.spi_clk_ratio) {
	case SPI_CLOCK_RATIO_1_VAL:
		tmp_param &= SPI_CLOCK_RATIO_1;
		break;
	case SPI_CLOCK_RATIO_2_VAL:
		tmp_param |= SPI_CLOCK_RATIO_2;
		break;
	default:
		my_printf(TERR, "\n\nInvalid SPI Core Clock Ratio (%d) ",
			  binary_params.spi_clk_ratio);
		my_printf(TERR, "- it should be 1 or 2");
		return FALSE;
	}

	if (!write_to_file(tmp_param, HDR_SPI_MAX_CLK_OFFSET, 1,
			   "HDR - SPI flash MAX Clock	  "))
		return FALSE;

	/* Write the SPI flash Read Mode. */
	tmp_param = binary_params.spi_read_mode;
	/* If needed, set the unlimited burst bit. */
	if (binary_params.bin_params & BIN_UNLIM_BURST_ENABLE) {
		if (is_mrider15 == TRUE) {
			my_printf(TERR, "\nunlimburst is not relevant for");
			my_printf(TERR, " npcx5mng chips family !\n");

			return FALSE;
		}

		tmp_param |= SPI_UNLIMITED_BURST_ENABLE;
	}
	if (!write_to_file(tmp_param, HDR_SPI_READ_MODE_OFFSET, 1,
			   "HDR - SPI flash Read Mode	   "))
		return FALSE;

	/* Write the error detection configuration. */
	if (binary_params.bin_params & BIN_FW_CRC_DISABLE) {
		if (!write_to_file(FW_CRC_DISABLE,
				   HDR_ERR_DETECTION_CONF_OFFSET, 1,
				   "HDR - FW CRC Disabled		   "))
			return FALSE;
	} else {
		/* Write the ancore and the extended anchor. */
		if (!write_to_file(FW_CRC_ENABLE, HDR_ERR_DETECTION_CONF_OFFSET,
				   1, "HDR - FW CRC Enabled		  "))
			return FALSE;
	}

	/* FW entry point should be between the FW load address and RAM size */
	if ((binary_params.fw_load_addr > (g_ram_start_address + g_ram_size)) ||
	    (binary_params.fw_load_addr < g_ram_start_address)) {
		my_printf(TERR, "\nFW load address (0x%08x) should be between ",
			  binary_params.fw_load_addr);
		my_printf(TERR, "start (0x%08x) and end (0x%08x) of RAM ).",
			  g_ram_start_address,
			  (g_ram_start_address + g_ram_size));

		return FALSE;
	}

	/* Write the FW load start address */
	if (!write_to_file(binary_params.fw_load_addr,
			   HDR_FW_LOAD_START_ADDR_OFFSET, 4,
			   "HDR - FW load start address	 "))
		return FALSE;

	/*
	 * Write the FW length. (MUST BE SET BEFORE fw_err_detec_e_addr)
	 */
	if ((binary_params.bin_params & BIN_FW_LENGTH) == 0x00000000) {
		/*
		 * In case the FW length was not set, then the FW length is the
		 * size of the binary file minus the offset of the start of the
		 * FW.
		 */
		binary_params.fw_len = bin_file_size_bytes - bin_fw_offset;
	}

	if ((int)binary_params.fw_len < 0) {
		my_printf(TERR,
			  "\nFW length %d (0x%08x) should be greater than 0x0.",
			  binary_params.fw_len, binary_params.fw_len);
		return FALSE;
	}

	if (((int)binary_params.fw_len >
	     (bin_file_size_bytes - bin_fw_offset)) ||
	    ((int)binary_params.fw_len > g_ram_size)) {
		my_printf(TERR, "\nFW length %d (0x%08x) should be within the",
			  binary_params.fw_len, binary_params.fw_len);
		my_printf(TERR, " input-file (related to the FW offset)");
		my_printf(TERR,
			  "\n (0x%08x) and within the RAM (RAM size: 0x%08x).",
			  (bin_file_size_bytes - bin_fw_offset), g_ram_size);
		return FALSE;
	}

	if ((binary_params.bin_params & BIN_FW_USER_ARM_RESET) != 0x00000000) {
		read_from_file((bin_fw_offset + ARM_FW_ENTRY_POINT_OFFSET), 4,
			       &binary_params.fw_ep,
			       "read FW entry point for FW image ");

		if ((binary_params.fw_ep < binary_params.fw_load_addr) ||
		    (binary_params.fw_ep >
		     (binary_params.fw_load_addr + binary_params.fw_len))) {
			my_printf(TERR,
				  "\nFW entry point (0x%08x) should be between",
				  binary_params.fw_ep);
			my_printf(TERR, " the FW load address (0x%08x) ",
				  binary_params.fw_load_addr);
			my_printf(TERR, "and FW length (0x%08x).\n",
				  (binary_params.fw_load_addr +
				   binary_params.fw_len));
			return FALSE;
		}
	}

	/* FW entry point should be between the FW load address and RAM size */
	if ((binary_params.fw_ep < binary_params.fw_load_addr) ||
	    (binary_params.fw_ep >
	     (binary_params.fw_load_addr + binary_params.fw_len))) {
		if (((binary_params.bin_params & BIN_FW_ENTRY_POINT) ==
		     0x00000000) &&
		    ((binary_params.bin_params & BIN_FW_LOAD_START_ADDR) !=
		     0x00000000)) {
			binary_params.fw_ep = binary_params.fw_load_addr;
		} else {
			my_printf(TERR, "\nFW entry point (0x%08x) should be ",
				  binary_params.fw_ep);
			my_printf(TERR, "\between the FW load address (0x%08x)",
				  binary_params.fw_load_addr);
			my_printf(TERR, " and FW length (0x%08x).\n",
				  (binary_params.fw_load_addr +
				   binary_params.fw_len));
			return FALSE;
		}
	}

	/* Write the FW entry point */
	if (!write_to_file(binary_params.fw_ep, HDR_FW_ENTRY_POINT_OFFSET, 4,
			   "HDR - FW Entry point		 "))
		return FALSE;

	/* Calculate the CRC end address. */
	if ((binary_params.bin_params & BIN_FW_CKS_SIZE) == 0x00000000) {
		/*
		 * In case the size was not set, then CRC end address is
		 * the size of the binary file.
		 */
		binary_params.fw_err_detec_e_addr = binary_params.fw_len - 1;
	} else {
		/* CRC end address should be less than FW length. */
		if (binary_params.fw_err_detec_e_addr >
		    (binary_params.fw_len - 1)) {
			my_printf(TERR,
				  "\nCRC end address (0x%08x) should be less ",
				  binary_params.fw_err_detec_e_addr);
			my_printf(TERR, "than the FW length %d (0x%08x)",
				  (binary_params.fw_len),
				  (binary_params.fw_len));
			return FALSE;
		}
	}

	/* Check CRC start and end addresses. */
	if (binary_params.fw_err_detec_s_addr >
	    binary_params.fw_err_detec_e_addr) {
		my_printf(TERR,
			  "\nCRC start address (0x%08x) should be less or ",
			  binary_params.fw_err_detec_s_addr);
		my_printf(TERR,
			  "equal to CRC end address (0x%08x)\nPlease check ",
			  binary_params.fw_err_detec_e_addr);
		my_printf(TERR, "CRC start address and CRC size arguments.");
		return FALSE;
	}

	/* CRC start addr should be between the FW load address and RAM size */
	if (binary_params.fw_err_detec_s_addr > binary_params.fw_len) {
		my_printf(TERR, "\nCRC start address (0x%08x) should ",
			  binary_params.fw_err_detec_s_addr);
		my_printf(TERR, "be FW length (0x%08x).", binary_params.fw_len);
		return FALSE;
	}

	/* Write the CRC start address */
	if (!write_to_file(binary_params.fw_err_detec_s_addr,
			   HDR_FW_ERR_DETECT_START_ADDR_OFFSET, 4,
			   "HDR - FW CRC Start			 "))
		return FALSE;

	/* CRC end addr should be between the CRC start address and RAM size */
	if ((binary_params.fw_err_detec_e_addr <
	     binary_params.fw_err_detec_s_addr) ||
	    (binary_params.fw_err_detec_e_addr > binary_params.fw_len)) {
		my_printf(TERR,
			  "\nCRC end address (0x%08x) should be between the ",
			  binary_params.fw_err_detec_e_addr);
		my_printf(TERR,
			  "CRC start address (0x%08x) and FW length (0x%08x).",
			  binary_params.fw_err_detec_s_addr,
			  binary_params.fw_len);
		return FALSE;
	}

	/* Write the CRC end address */
	if (!write_to_file(binary_params.fw_err_detec_e_addr,
			   HDR_FW_ERR_DETECT_END_ADDR_OFFSET, 4,
			   "HDR - FW CRC End			 "))
		return FALSE;

	/* Let the FW length to be aligned to 16 */
	tmp_param = binary_params.fw_len % 16;
	if (tmp_param)
		binary_params.fw_len += (16 - tmp_param);

	/* FW load address + FW length should be less than the RAM size. */
	if ((binary_params.fw_load_addr + binary_params.fw_len) >
	    (g_ram_start_address + g_ram_size)) {
		my_printf(TERR,
			  "\nFW load address + FW length should (0x%08x) be ",
			  (binary_params.fw_load_addr + binary_params.fw_len));
		my_printf(TERR, "less than the RAM size (0x%08x).",
			  (g_ram_start_address + g_ram_size));
		return FALSE;
	}

	/* Write the FW length */
	if (!write_to_file(binary_params.fw_len, HDR_FW_LENGTH_OFFSET, 4,
			   "HDR - FW Length			   "))
		return FALSE;

	/* Write the SPI flash MAX clock. */
	switch (binary_params.flash_size) {
	case FLASH_SIZE_1_MBYTES_VAL:
		tmp_param = FLASH_SIZE_1_MBYTES;
		break;
	case FLASH_SIZE_2_MBYTES_VAL:
		tmp_param = FLASH_SIZE_2_MBYTES;
		break;
	case FLASH_SIZE_4_MBYTES_VAL:
		tmp_param = FLASH_SIZE_4_MBYTES;
		break;
	case FLASH_SIZE_8_MBYTES_VAL:
		tmp_param = FLASH_SIZE_8_MBYTES;
		break;
	case FLASH_SIZE_16_MBYTES_VAL:
		tmp_param = FLASH_SIZE_16_MBYTES;
		break;
	default:
		my_printf(TERR, "\n\nInvalid Flash size (%d MBytes) -",
			  binary_params.flash_size);
		my_printf(TERR, " it should be 1, 2, 4, 8 or 16 MBytes\n");
		return FALSE;
	}
	if (!write_to_file(tmp_param, HDR_FLASH_SIZE_OFFSET, 1,
			   "HDR - Flash size			 "))

		return FALSE;

	/* Write the reserved bytes. */
	if (!write_to_file(PAD_VALUE, HDR_RESERVED, 26,
			   "HDR - Reserved (26 bytes)	  "))
		return FALSE;

	/* Refresh the FW header bin file in order to calculate CRC */
	if (g_hfd_pointer) {
		fclose(g_hfd_pointer);
		g_hfd_pointer = fopen(g_hdr_input_name, "r+b");
		if (g_hfd_pointer == NULL) {
			my_printf(TERR, "\n\nCannot open %s\n\n",
				  input_file_name);
			return FALSE;
		}
	}

	/* Calculate the FW header CRC */
	if ((binary_params.bin_params & BIN_FW_HDR_CRC_DISABLE) == 0) {
		/* Calculate ... */
		g_calc_type = CALC_TYPE_CRC;
		if (!calc_header_crc_bin(&binary_params.hdr_crc))
			return FALSE;

		g_calc_type = CALC_TYPE_NONE;
	} else
		binary_params.hdr_crc = 0;

	/* Write FW header CRC to header file */
	if (!write_to_file(binary_params.hdr_crc, HDR_FW_HEADER_SIG_OFFSET, 4,
			   "HDR - Header CRC				"))
		return FALSE;

	/* Calculate the FW	 CRC */
	if ((binary_params.bin_params & BIN_FW_CRC_DISABLE) == 0) {
		/* Calculate ... */
		g_calc_type = CALC_TYPE_CRC;
		if (!calc_firmware_csum_bin(
			    &binary_params.fw_crc,
			    (bin_fw_offset + binary_params.fw_err_detec_s_addr),
			    (binary_params.fw_err_detec_e_addr -
			     binary_params.fw_err_detec_s_addr + 1)))
			return FALSE;

		g_calc_type = CALC_TYPE_NONE;
	} else
		binary_params.fw_crc = 0;

	/* Write the FW CRC into file header file */
	if (!write_to_file(binary_params.fw_crc, HDR_FW_IMAGE_SIG_OFFSET, 4,
			   "HDR - FW CRC				   "))
		return FALSE;

	/* Close if needed... */
	if (input_file_pointer) {
		fclose(input_file_pointer);
		input_file_pointer = NULL;
	}

	if (g_hfd_pointer) {
		fclose(g_hfd_pointer);
		g_hfd_pointer = NULL;
	}

	/* Create empty output file. */
	output_file_pointer = fopen(output_file_name, "wb");
	if (output_file_pointer)
		fclose(output_file_pointer);

	if ((binary_params.bin_params & BIN_FW_HDR_OFFSET) != 0) {
		copy_file_to_file(output_file_name, input_file_name, 0,
				  SEEK_SET);
		copy_file_to_file(output_file_name, g_hdr_input_name,
				  binary_params.fw_hdr_offset, SEEK_SET);
	} else {
		copy_file_to_file(output_file_name, g_hdr_input_name, 0,
				  SEEK_END);
		copy_file_to_file(output_file_name, input_file_name, 0,
				  SEEK_END);
	}

	my_printf(TINF, "\n\n");

	return TRUE;
}

/*******************************************************************
 * Function:	calc_header_crc_bin
 * Parameters:	unsigned short header checksum (O)
 *		unsigned int header offset from first byte in
 *		the binary (I)
 * Return:	TRUE if successful
 * Description:	 Go thru bin file and calculate checksum
 *******************************************************************
 */
int calc_header_crc_bin(unsigned int *p_cksum)
{
	int i;
	unsigned int calc_header_checksum_crc = 0;
	unsigned char g_header_array[HEADER_SIZE];
	int line_print_size = 32;

	init_calculation(&calc_header_checksum_crc);

	/* Go thru the BIN File and calculate the Checksum */
	if (fseek(g_hfd_pointer, 0x00000000, SEEK_SET) < 0)
		return FALSE;

	if (fread(g_header_array, HEADER_SIZE, 1, g_hfd_pointer) != 1)
		return FALSE;

	for (i = 0; i < (HEADER_SIZE - HEADER_CRC_FIELDS_SIZE); i++) {
		/*
		 * I had once the Verbose check inside the my_printf, but
		 * it made ECST run sloooowwwwwly....
		 */
		if (g_verbose == SUPER_VERBOSE) {
			if (i % line_print_size == 0)
				my_printf(TDBG, "\n[%.4x]: ", i);

			my_printf(TDBG, "%.2x ", g_header_array[i]);
		}

		update_calculation(&calc_header_checksum_crc,
				   g_header_array[i]);

		if (g_verbose == SUPER_VERBOSE) {
			if ((i + 1) % line_print_size == 0)
				my_printf(TDBG, "FW Header ChecksumCRC = %.8x",
					  calc_header_checksum_crc);
		}
	}

	finalize_calculation(&calc_header_checksum_crc);
	*p_cksum = calc_header_checksum_crc;

	return TRUE;
}

/*
 *******************************************************************
 * Function:	 calc_firmware_csum_bin
 * Parameters:	 unsigned int fwStart (I)
 *				 unsigned int firmware size in words (I)
 *				 unsigned int - firmware checksum (O)
 * Return:
 * Description:	 TBD
 *******************************************************************
 */
int calc_firmware_csum_bin(unsigned int *p_cksum, unsigned int fw_offset,
			   unsigned int fw_length)
{
	unsigned int i;
	unsigned int calc_read_bytes;
	unsigned int calc_num_of_bytes_to_read;
	unsigned int calc_curr_position;
	unsigned int calc_fw_checksum_crc = 0;
	unsigned char g_fw_array[BUFF_SIZE];
	int line_print_size = 32;

	calc_num_of_bytes_to_read = fw_length;
	calc_curr_position = fw_offset;

	if (g_verbose == REGULAR_VERBOSE) {
		my_printf(TINF, "\nFW Error Detect Start Dddress: 0x%08x",
			  calc_curr_position);
		my_printf(TINF, "\nFW Error Detect End Dddress: 0x%08x",
			  calc_curr_position + calc_num_of_bytes_to_read - 1);
		my_printf(TINF, "\nFW Error Detect Size:  %d (0x%X)",
			  calc_num_of_bytes_to_read, calc_num_of_bytes_to_read);
	}

	init_calculation(&calc_fw_checksum_crc);

	while (calc_num_of_bytes_to_read > 0) {
		if (calc_num_of_bytes_to_read > BUFF_SIZE)
			calc_read_bytes = BUFF_SIZE;
		else
			calc_read_bytes = calc_num_of_bytes_to_read;

		if (fseek(input_file_pointer, calc_curr_position, SEEK_SET) < 0)
			return 0;
		if (fread(g_fw_array, calc_read_bytes, 1, input_file_pointer) !=
		    1)
			return 0;

		for (i = 0; i < calc_read_bytes; i++) {
			/*
			 * I had once the Verbose check inside the my_printf,
			 * but it made ECST run sloooowwwwwly....
			 */
			if (g_verbose == SUPER_VERBOSE) {
				if (i % line_print_size == 0)
					my_printf(TDBG, "\n[%.4x]: ",
						  calc_curr_position + i);

				my_printf(TDBG, "%.2x ", g_fw_array[i]);
			}

			update_calculation(&calc_fw_checksum_crc,
					   g_fw_array[i]);

			if (g_verbose == SUPER_VERBOSE) {
				if ((i + 1) % line_print_size == 0)
					my_printf(TDBG, "FW Checksum= %.8x",
						  calc_fw_checksum_crc);
			}
		}
		calc_num_of_bytes_to_read -= calc_read_bytes;
		calc_curr_position += calc_read_bytes;
	} /* end of while(calc_num_of_bytes_to_read > 0) */

	finalize_calculation(&calc_fw_checksum_crc);
	*p_cksum = calc_fw_checksum_crc;

	return TRUE;
}

/*
 ***************************************************************************
 *			"bh" mode Handler
 ***************************************************************************
 */

/*
 *******************************************************************
 * Function:	 main_hdr
 * Parameters:	 TBD
 * Return:	 True for success
 * Description:
 *******************************************************************
 */
int main_hdr(void)
{
	int result = 0;
	char tmp_file_name[NAME_SIZE + 1];
	unsigned int tmp_long_val;
	unsigned int bin_file_size_bytes;

	tmp_file_name[NAME_SIZE] = '\0';

	if (is_ptr_merge) {
		if (strlen(input_file_name) == 0) {
			my_printf(TERR, "\n\nNo input BIN file selected for");
			my_printf(TERR, " BootLoader header file.\n\n");
			return FALSE;
		}

		if (strlen(output_file_name) == 0)
			strncpy(tmp_file_name, input_file_name,
				sizeof(tmp_file_name) - 1);
		else {
			copy_file_to_file(output_file_name, input_file_name, 0,
					  SEEK_END);
			strncpy(tmp_file_name, output_file_name,
				sizeof(tmp_file_name) - 1);
		}

		/* Open Header file */
		g_hdr_pointer = fopen(tmp_file_name, "r+b");
		if (g_hdr_pointer == NULL) {
			my_printf(TERR, "\n\nCannot open %s file.\n\n",
				  tmp_file_name);
			return FALSE;
		}

		bin_file_size_bytes = get_file_length(g_hdr_pointer);

		/* Offset should be less than file size. */
		if (fw_offset > bin_file_size_bytes) {
			my_printf(TERR,
				  "\n\nFW offset 0x%08x should be less than ",
				  fw_offset);
			my_printf(TERR, "file size 0x%x (%d).\n\n",
				  bin_file_size_bytes, bin_file_size_bytes);
			return FALSE;
		}

		/* FW table should be less than file size. */
		if (ptr_fw_addr > bin_file_size_bytes) {
			my_printf(TERR, "\n\nFW table 0x%08x should be less ",
				  ptr_fw_addr);
			my_printf(TERR, "than file size 0x%x (%d).\n\n",
				  bin_file_size_bytes, bin_file_size_bytes);
			return FALSE;
		}

		if (fseek(g_hdr_pointer, fw_offset, SEEK_SET) < 0)
			return FALSE;

		tmp_long_val = HDR_PTR_SIGNATURE;
		result = (int)(fwrite(&tmp_long_val, 4, 1, g_hdr_pointer));
		result |= (int)(fwrite(&ptr_fw_addr, 4, 1, g_hdr_pointer));

		if (result) {
			my_printf(TINF, "\nBootLoader Header file: %s\n",
				  tmp_file_name);
			my_printf(TINF, " Offset: 0x%08X,  Signature: 0x%08X,",
				  fw_offset, HDR_PTR_SIGNATURE);
			my_printf(TINF, " Pointer: 0x%08X\n", ptr_fw_addr);
		} else {
			my_printf(TERR,
				  "\n\nCouldn't write signature (%x) and "
				  "pointer to BootLoader header file (%s)\n\n",
				  tmp_long_val, tmp_file_name);
			return FALSE;
		}

	} else {
		if (strlen(output_file_name) == 0) {
			my_printf(TERR, "\n\nNo output file selected ");
			my_printf(TERR, "for BootLoader header file.\n\n");
			return FALSE;
		}

		/* Open Output file */
		g_hdr_pointer = fopen(output_file_name, "w+b");
		if (g_hdr_pointer == NULL) {
			my_printf(TERR, "\n\nCannot open %s file.\n\n",
				  output_file_name);
			return FALSE;
		}

		if (fseek(g_hdr_pointer, 0L, SEEK_SET) < 0)
			return FALSE;

		tmp_long_val = HDR_PTR_SIGNATURE;
		result = (int)(fwrite(&tmp_long_val, 4, 1, g_hdr_pointer));
		result |= (int)(fwrite(&ptr_fw_addr, 4, 1, g_hdr_pointer));

		if (result) {
			my_printf(TINF, "\nBootLoader Header file: %s\n",
				  output_file_name);
			my_printf(TINF,
				  "     Signature: 0x%08X,   Pointer: 0x%08X\n",
				  HDR_PTR_SIGNATURE, ptr_fw_addr);
		} else {
			my_printf(TERR,
				  "\n\nCouldn't write signature (%x) and ",
				  tmp_long_val);
			my_printf(TERR,
				  "pointer to BootLoader header file (%s)\n\n",
				  output_file_name);
			return FALSE;
		}
	}

	/* Close if needed... */
	if (g_hdr_pointer) {
		fclose(g_hdr_pointer);
		g_hdr_pointer = NULL;
	}

	return TRUE;
}

/*
 ***************************************************************************
 *		"api" mode Handler
 ***************************************************************************
 */

/*
 *******************************************************************
 * Function:		main_api
 * Parameters:		TBD
 * Return:			True for success
 * Description:
 *	TBD
 *******************************************************************
 */
int main_api(void)
{
	char tmp_file_name[NAME_SIZE + 1];
	int result = 0;
	unsigned int crc_checksum;

	tmp_file_name[NAME_SIZE] = '\0';
	api_file_size_bytes = 0;

	/* If API input file was not declared, then print error message. */
	if (strlen(input_file_name) == 0) {
		my_printf(
			TERR,
			"\n\nNeed to define API input file, using -i flag\n\n");
		return FALSE;
	}

	if (strlen(output_file_name) == 0) {
		if (!splice_into_path(tmp_file_name, input_file_name,
				      sizeof(tmp_file_name), "api_"))
			return FALSE;
	} else
		strncpy(tmp_file_name, output_file_name,
			sizeof(tmp_file_name) - 1);

	/* Make sure that new empty file is created. */
	api_file_pointer = fopen(tmp_file_name, "w");
	if (api_file_pointer == NULL) {
		my_printf(TERR, "\n\nCannot open %s\n\n", tmp_file_name);
		return FALSE;
	}
	fclose(api_file_pointer);

	copy_file_to_file(tmp_file_name, input_file_name, 0, SEEK_END);

	/* Open API input file */
	api_file_pointer = fopen(tmp_file_name, "r+b");
	if (api_file_pointer == NULL) {
		my_printf(TERR, "\n\nCannot open %s\n\n", tmp_file_name);
		return FALSE;
	}

	/*
	 * Check Binary file size, this file contain the image itself,
	 * without any header.
	 */
	api_file_size_bytes = get_file_length(api_file_pointer);
	if (api_file_size_bytes < 0)
		return FALSE;
	my_printf(TINF, "\nAPI file: %s, size: %d bytes (0x%x)\n",
		  tmp_file_name, api_file_size_bytes, api_file_size_bytes);

	crc_checksum = calc_api_csum_bin();

	if (fseek(api_file_pointer, api_file_size_bytes, SEEK_SET) < 0)
		return FALSE;

	result = (int)(fwrite(&crc_checksum, 4, 1, api_file_pointer));

	if (result)
		my_printf(TINF,
			  "\nIn API BIN file - Offset 0x%08X - value 0x%08X",
			  api_file_size_bytes, crc_checksum);
	else {
		my_printf(TERR,
			  "\n\nCouldn't write %x to API BIN file at %08x\n\n",
			  crc_checksum, api_file_size_bytes);
		return FALSE;
	}

	/* Close if needed... */
	if (api_file_pointer) {
		fclose(api_file_pointer);
		api_file_pointer = NULL;
	}

	return TRUE;
}

/*
 *******************************************************************
 * Function: calc_api_csum_bin
 * Parameters:
 *
 * Return: Return the CRC \ checksum, or "0" in case of fail.
 * Description:	 TBD
 *******************************************************************
 */
unsigned int calc_api_csum_bin(void)
{
	unsigned int i;
	unsigned int calc_read_bytes;
	int calc_num_of_bytes_to_read;
	unsigned int calc_curr_position;
	unsigned int calc_fw_checksum_crc = 0;
	unsigned char g_fw_array[BUFF_SIZE];
	int line_print_size = 32;

	calc_num_of_bytes_to_read = api_file_size_bytes;
	calc_curr_position = 0;

	if (g_verbose == SUPER_VERBOSE) {
		my_printf(TDBG,
			  "\nAPI CRC \\ Checksum First Byte Address: 0x%08x",
			  calc_curr_position);
		my_printf(TDBG, "\nAPI CRC \\ Checksum Size:  %d (0x%X)",
			  calc_num_of_bytes_to_read, calc_num_of_bytes_to_read);
	}

	init_calculation(&calc_fw_checksum_crc);

	while (calc_num_of_bytes_to_read > 0) {
		if (calc_num_of_bytes_to_read > BUFF_SIZE)
			calc_read_bytes = BUFF_SIZE;
		else
			calc_read_bytes = calc_num_of_bytes_to_read;

		if (fseek(api_file_pointer, calc_curr_position, SEEK_SET) < 0)
			return 0;
		if (fread(g_fw_array, calc_read_bytes, 1, api_file_pointer) !=
		    1)
			return 0;

		for (i = 0; i < calc_read_bytes; i++) {
			/*
			 * I had once the Verbose check inside the my_printf,
			 * but it made ecst run sloooowwwwwly....
			 */
			if (g_verbose == SUPER_VERBOSE) {
				if (i % line_print_size == 0)
					my_printf(TDBG, "\n[%.4x]: ",
						  calc_curr_position + i);

				my_printf(TDBG, "%.2x ", g_fw_array[i]);
			}

			update_calculation(&calc_fw_checksum_crc,
					   g_fw_array[i]);

			if (g_verbose == SUPER_VERBOSE) {
				if ((i + 1) % line_print_size == 0)
					my_printf(TDBG, "FW Checksum= %.8x",
						  calc_fw_checksum_crc);
			}
		}
		calc_num_of_bytes_to_read -= calc_read_bytes;
		calc_curr_position += calc_read_bytes;
	} /* end of while(calc_num_of_bytes_to_read > 0) */

	finalize_calculation(&calc_fw_checksum_crc);

	return calc_fw_checksum_crc;
}

/*
 **************************************************************************
 *			CRC Handler
 **************************************************************************
 */

/*
 *******************************************************************
 *
 * #define P_xxxx
 *
 * The CRC's are computed using polynomials. The  coefficients
 * for the algorithms are defined by the following constants.
 *
 *******************************************************************
 */

#define P_32 0xEDB88320L

/*
 *******************************************************************
 *
 *	static int crc_tab...init
 *	static unsigned int ... crc_tab...[]
 *
 *	The algorithms use tables with pre-calculated  values.  This
 *	speeds	up	the calculation dramatically. The first time the
 *	CRC function is called, the table for that specific	 calcu-
 *	lation	is set up. The ...init variables are used to deter-
 *	mine if the initialization has taken place. The	 calculated
 *	values are stored in the crc_tab... arrays.
 *
 *	The variables are declared static. This makes them invisible
 *	for other modules of the program.
 *
 *******************************************************************
 */
static int crc_tab32_init = FALSE;
static unsigned int crc_tab32[256];

/*
 ********************************************************************
 *
 *	static void init_crc...tab();
 *
 *	Three local functions are used to initialize the tables
 *	with values for the algorithm.
 *
 *******************************************************************
 */

static void init_crc32_tab(void);

/*
 *******************************************************************
 *
 * unsigned int initialize_crc_32( void );
 *
 * The function update_crc_32 calculates a new  CRC-32  value
 * based  on  the previous value of the CRC and the next byte
 * of the data to be checked.
 *
 *******************************************************************
 */

unsigned int initialize_crc_32(void)
{
	return 0xffffffffL;
} /* initialize_crc_32 */

/*
 *******************************************************************
 *
 * unsigned int update_crc_32( unsigned int crc, char c );
 *
 * The function update_crc_32 calculates a new  CRC-32  value
 * based  on  the previous value of the CRC and the next byte
 * of the data to be checked.
 *
 *******************************************************************
 */

unsigned int update_crc_32(unsigned int crc, char c)
{
	unsigned int tmp, long_c;

	long_c = 0x000000ffL & (unsigned int)c;

	if (!crc_tab32_init)
		init_crc32_tab();

	tmp = crc ^ long_c;
	crc = (crc >> 8) ^ crc_tab32[tmp & 0xff];

	return crc;

} /* update_crc_32 */

/*
 *******************************************************************
 *
 *	static void init_crc32_tab( void );
 *
 *	The function init_crc32_tab() is used  to  fill the  array
 *	for calculation of the CRC-32 with values.
 *
 *******************************************************************
 */
static void init_crc32_tab(void)
{
	int i, j;
	unsigned int crc;

	for (i = 0; i < 256; i++) {
		crc = (unsigned int)i;

		for (j = 0; j < 8; j++) {
			if (crc & 0x00000001L)
				crc = (crc >> 1) ^ P_32;
			else
				crc = crc >> 1;
		}

		crc_tab32[i] = crc;
	}

	crc_tab32_init = TRUE;

} /* init_crc32_tab */

/*
 *******************************************************************
 *
 * unsigned int finalize_crc_32( unsigned int crc );
 *
 * The function finalize_crc_32 finalizes a  CRC-32 calculation
 * by performing a bit convolution (bit 0 is bit 31, etc').
 *
 *******************************************************************
 */

unsigned int finalize_crc_32(unsigned int crc)
{
	int i;
	unsigned int result = 0;

	for (i = 0; i < NUM_OF_BYTES; i++)
		SET_VAR_BIT(result, NUM_OF_BYTES - (i + 1),
			    READ_VAR_BIT(crc, i));

	return result;

} /* finalize_crc_32 */
