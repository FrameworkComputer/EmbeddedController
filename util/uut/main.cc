/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "com_port.h"
#include "compile_time_macros.h"
#include "main.h"
#include "misc_util.h"
#include "opr.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <sys/stat.h>

/*----------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */

#define MAX_FILE_NAME_SIZE 512
#define MAX_PARAM_SIZE 32
#define MAX_MSG_SIZE 128
#define MAX_SYNC_RETRIES 3

/* Default values */
#define DEFAULT_BAUD_RATE 115200
#define DEFAULT_PORT_NAME "ttyS0"
#define DEFAULT_DEV_NUM 0
#define DEFAULT_FLASH_OFFSET 0

/* The magic number in monitor header */
#define MONITOR_HDR_TAG 0xA5075001
/* The location of monitor header */
#define MONITOR_HDR_ADDR 0x200C3000
/* The start address of the monitor little firmware to execute */
#define MONITOR_ADDR 0x200C3020
/* The start address to store the firmware segment to be programmed */
#define FIRMWARE_START_ADDR 0x10090000
/* Divide the ec firmware image into 4K byte */
#define FIRMWARE_SEGMENT 0x1000
/* Register address for chip ID */
#define NPCX_SRID_CR 0x400C101C
/* Register address for device ID */
#define NPCX_DEVICE_ID_CR 0x400C1022
#define NPCX_FLASH_BASE_ADDR 0x64000000

/*---------------------------------------------------------------------------
 * Global variables
 *---------------------------------------------------------------------------
 */
bool verbose;
bool console;
struct comport_fields port_cfg;

/*---------------------------------------------------------------------------
 * Local variables
 *---------------------------------------------------------------------------
 */

static const char tool_name[] = { "LINUX UART Update Tool" };
static const char tool_version[] = { "2.0.1" };

static char port_name[MAX_PARAM_SIZE];
static char opr_name[MAX_PARAM_SIZE];
static char file_name[MAX_FILE_NAME_SIZE];
static char addr_str[MAX_PARAM_SIZE];
static char size_str[MAX_PARAM_SIZE];
static uint32_t baudrate;
static uint32_t dev_num;
static uint32_t flash_offset;
static bool auto_mode;
static bool read_flash_flag;

struct npcx_chip_info {
	uint8_t device_id;
	uint8_t chip_id;
	uint32_t flash_size;
};

const static struct npcx_chip_info chip_info[] = {
	{
		/* NPCX796FA */
		.device_id = 0x21,
		.chip_id = 0x06,
		.flash_size = 1024 * 1024,
	},

	{
		/* NPCX796FB */
		.device_id = 0x21,
		.chip_id = 0x07,
		.flash_size = 1024 * 1024,
	},

	{
		/* NPCX796FC */
		.device_id = 0x29,
		.chip_id = 0x07,
		.flash_size = 512 * 1024,
	},

	{
		/* NPCX797FC */
		.device_id = 0x20,
		.chip_id = 0x07,
		.flash_size = 512 * 1024,
	},

	{
		/* NPCX797WB */
		.device_id = 0x24,
		.chip_id = 0x07,
		.flash_size = 1024 * 1024,
	},

	{
		/* NPCX797WC */
		.device_id = 0x2C,
		.chip_id = 0x07,
		.flash_size = 512 * 1024,
	},

	{
		/* NPCX993F */
		.device_id = 0x25,
		.chip_id = 0x09,
		.flash_size = 512 * 1024,
	},

	{
		/* NPCX996F */
		.device_id = 0x21,
		.chip_id = 0x09,
		.flash_size = 512 * 1024,
	},
	{
		/* NPCX997F */
		.device_id = 0x22,
		.chip_id = 0x09,
		.flash_size = 1024 * 1024,
	},
	{
		/* NPCX99FP */
		.device_id = 0x2B,
		.chip_id = 0x09,
		.flash_size = 1024 * 1024,
	},
};
/*---------------------------------------------------------------------------
 * Functions prototypes
 *---------------------------------------------------------------------------
 */

static void param_parse_cmd_line(int argc, char *argv[]);
static void param_check_opr_num(const char *opr);
static uint32_t param_get_file_size(const char *file_name);
static uint32_t param_get_str_size(char *string);
static void main_print_version(void);
static void tool_usage(void);
static void exit_uart_app(int32_t exit_status);

enum EXIT_CODE {
	EC_OK = 0x00,
	EC_PORT_ERR = 0x01,
	EC_BAUDRATE_ERR = 0x02,
	EC_SYNC_ERR = 0x03,
	EC_DEV_NUM_ERR = 0x04,
	EC_OPR_MUM_ERR = 0x05,
	EC_ALIGN_ERR = 0x06,
	EC_FILE_ERR = 0x07,
	EC_UNSUPPORTED_CMD_ERR = 0x08
};

/*---------------------------------------------------------------------------
 * Function implementation
 *---------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------
 * Function:	image_auto_write
 *
 * Parameters:    offset - The start address of the flash to write the firmware
 *                         to.
 *                buffer - The buffer holds data of the firmware.
 *                file_size - the size to be programmed.
 * Returns:       1 for a successful operation, 0 otherwise.
 * Side effects:
 * Description:
 *               Divide the firmware to segments and program them one by one.
 *---------------------------------------------------------------------------
 */
static bool image_auto_write(uint32_t offset, uint8_t *buffer,
			     uint32_t file_size)
{
	uint32_t data_buf[4];
	uint32_t addr, chunk_remain, file_seg, flash_index, seg;
	uint32_t count, percent, total;
	uint32_t i;

	flash_index = offset;
	/* Monitor tag */
	data_buf[0] = MONITOR_HDR_TAG;

	file_seg = file_size;
	total = 0;
	while (file_seg) {
		seg = (file_seg > FIRMWARE_SEGMENT) ? FIRMWARE_SEGMENT :
						      file_seg;
		/*
		 * Check if the content of the segment is all 0xff.
		 * If yes, there is no need to write.
		 * Call the monitor to erase the segment only for
		 * time efficiency.
		 */
		for (i = 0; i < seg && buffer[i] == 0xFF; i++)
			;
		if (i == seg) {
			data_buf[1] = seg;
			/*
			 * Set src_addr = 0 as a flag. When the monitor read 0
			 * from this field, it will do sector erase only.
			 */
			data_buf[2] = 0;
			data_buf[3] = flash_index;
			opr_write_chunk((uint8_t *)data_buf, MONITOR_HDR_ADDR,
					sizeof(data_buf));
			if (opr_execute_return(MONITOR_ADDR) != true)
				return false;
			file_seg -= seg;
			flash_index += seg;
			buffer += seg;
			total += seg;
			percent = total * 100 / file_size;
			printf("\r[%d%%] %d/%d", percent, total, file_size);
			fflush(stdout);
			continue;
		}
		chunk_remain = seg;
		addr = FIRMWARE_START_ADDR;
		/* the size to be programmed */
		data_buf[1] = seg;
		/* The source(RAM) address where the firmware is stored. */
		data_buf[2] = FIRMWARE_START_ADDR;
		/*
		 * The offset of the flash where the segment to be programmed.
		 */
		data_buf[3] = flash_index;
		/* Write the monitor header to RAM */
		opr_write_chunk((uint8_t *)data_buf, MONITOR_HDR_ADDR,
				sizeof(data_buf));
		while (chunk_remain) {
			count = (chunk_remain > MAX_RW_DATA_SIZE) ?
					MAX_RW_DATA_SIZE :
					chunk_remain;
			if (opr_write_chunk(buffer, addr, count) != true)
				return false;

			addr += count;
			buffer += count;
			chunk_remain -= count;
			total += count;
			percent = total * 100 / file_size;
			printf("\r[%d%%] %d/%d", percent, total, file_size);
		}
		fflush(stdout);
		if (opr_execute_return(MONITOR_ADDR) != true)
			return false;
		file_seg -= seg;
		flash_index += seg;
	}
	printf("\n");
	/* Clear the UUT header tag */
	data_buf[0] = 0;
	opr_write_chunk((uint8_t *)data_buf, MONITOR_HDR_ADDR, 4);
	return true;
}

static bool get_flash_size(uint32_t *flash_size)
{
	uint8_t dev_id, chip_id, i;

	if (opr_read_chunk(&dev_id, NPCX_DEVICE_ID_CR, 1) != true)
		return false;

	if (opr_read_chunk(&chip_id, NPCX_SRID_CR, 1) != true)
		return false;

	for (i = 0; i < ARRAY_SIZE(chip_info); i++) {
		if (chip_info[i].device_id == dev_id &&
		    chip_info[i].chip_id == chip_id) {
			*flash_size = chip_info[i].flash_size;
			return true;
		}
	}
	printf("Unknown NPCX device ID:0x%02x chip ID:0x%02x\n", dev_id,
	       chip_id);

	return false;
}

/*---------------------------------------------------------------------------
 * Function:	read_input_file
 *
 * Parameters:    size - The size of input file.
 *                file_name - The name if input file.
 * Returns:       The address of the buffer allocated to stroe file content.
 * Side effects:
 * Description:
 *               Read the file content into an allocated buffer.
 *---------------------------------------------------------------------------
 */
static uint8_t *read_input_file(uint32_t size, const char *file_name)
{
	uint8_t *buffer;
	FILE *input_fp;

	buffer = (uint8_t *)(malloc(size));
	if (!buffer) {
		fprintf(stderr, "Cannot allocate %d bytes\n", size);
		return NULL;
	}
	input_fp = fopen(file_name, "r");
	if (!input_fp) {
		display_color_msg(FAIL, "ERROR: cannot open file %s\n",
				  file_name);
		free(buffer);
		return NULL;
	}
	if (fread(buffer, size, 1, input_fp) != 1) {
		fprintf(stderr, "Cannot read %s\n", file_name);
		fclose(input_fp);
		free(buffer);
		return NULL;
	}
	fclose(input_fp);
	return buffer;
}

/*---------------------------------------------------------------------------
 * Function:	main
 *
 * Parameters:		argc - Argument Count.
 *			argv - Argument Vector.
 * Returns:		1 for a successful operation, 0 otherwise.
 * Side effects:
 * Description:
 *		Console application main operation.
 *---------------------------------------------------------------------------
 */
int main(int argc, char *argv[])
{
	char *stop_str;
	char aux_buf[MAX_FILE_NAME_SIZE];
	uint32_t size = 0;
	uint32_t addr = 0;
	enum sync_result sr;
	uint8_t *buffer;
	int sync_cnt;

	if (argc <= 1)
		exit(EC_UNSUPPORTED_CMD_ERR);

	/* Setup defaults */
	strncpy(port_name, DEFAULT_PORT_NAME, sizeof(port_name));
	baudrate = DEFAULT_BAUD_RATE;
	dev_num = DEFAULT_DEV_NUM;
	flash_offset = DEFAULT_FLASH_OFFSET;
	opr_name[0] = '\0';
	verbose = true;
	console = false;
	auto_mode = false;
	read_flash_flag = false;

	param_parse_cmd_line(argc, argv);

	/* Configure COM Port parameters */
	port_cfg.baudrate = MAX(baudrate, BR_LOW_LIMIT);
	port_cfg.byte_size = CS8;
	port_cfg.flow_control = 0;
	port_cfg.parity = 0;
	port_cfg.stop_bits = 0;

	/*
	 * Open a ComPort device. If user haven't specified such, use ComPort 1
	 */
	if (opr_open_port(port_name, port_cfg) != true)
		exit(EC_PORT_ERR);

	if (baudrate == 0) { /* Scan baud rate range */
		opr_scan_baudrate();
		exit_uart_app(EC_OK);
	}

	/* Verify Host and Device are synchronized */
	DISPLAY_MSG(("Performing a Host/Device synchronization check...\n"));
	for (sync_cnt = 1; sync_cnt <= MAX_SYNC_RETRIES; sync_cnt++) {
		sr = opr_check_sync(baudrate);
		if (sr == SR_OK)
			break;
		/*
		 * If it fails, try it again up to three times.
		 * It might fail for garbage data drainage from H1, or
		 * for timeout due to unstable data transfer yet.
		 */
		display_color_msg(
			FAIL,
			"Host/Device synchronization failed, error = %d,"
			" fail count = %d\n",
			sr, sync_cnt);
	}
	if (sync_cnt > MAX_SYNC_RETRIES)
		exit_uart_app(EC_SYNC_ERR);

	if (auto_mode) {
		size = param_get_file_size(file_name);
		if (size == 0)
			exit_uart_app(EC_FILE_ERR);

		buffer = read_input_file(size, file_name);
		if (!buffer)
			exit_uart_app(EC_FILE_ERR);

		printf("Write file %s at %d with %d bytes\n", file_name,
		       flash_offset, size);
		if (image_auto_write(flash_offset, buffer, size)) {
			printf("Flash Done.\n");
			free(buffer);
			exit_uart_app(EC_OK);
		}
		free(buffer);
		exit_uart_app(-1);
	}

	if (read_flash_flag) {
		uint32_t flash_size;

		if (get_flash_size(&flash_size)) {
			printf("Read %d bytes from flash...\n", flash_size);
			opr_read_mem(file_name, NPCX_FLASH_BASE_ADDR,
				     flash_size);
			exit_uart_app(EC_OK);
		}

		printf("Fail to read the flash size\n");
		exit_uart_app(-1);
	}

	param_check_opr_num(opr_name);

	/* Write buffer data to chosen address */
	if (strcmp(opr_name, OPR_WRITE_MEM) == 0) {
		addr = strtoull(addr_str, &stop_str, 0);

		if (console) {
			/*
			 * Copy the input string to an auxiliary buffer, since
			 * string is altered by param_get_str_size
			 */
			memcpy(aux_buf, file_name, sizeof(file_name));
			/* Retrieve input size */
			size = param_get_str_size(file_name);
			/* Ensure non-zero size */
			if (size == 0)
				exit_uart_app(EC_FILE_ERR);
			opr_write_mem((uint8_t *)(aux_buf), addr, size);
		} else {
			size = param_get_file_size(file_name);
			if (size == 0)
				exit_uart_app(EC_FILE_ERR);
			buffer = read_input_file(size, file_name);
			if (!buffer)
				exit_uart_app(EC_FILE_ERR);
			opr_write_mem(buffer, addr, size);
			free(buffer);
		}
	} else if (strcmp(opr_name, OPR_READ_MEM) == 0) {
		/* Read data to chosen address */

		addr = strtoull(addr_str, &stop_str, 0);
		size = strtoull(size_str, &stop_str, 0);

		opr_read_mem(file_name, addr, size);
	} else if (strcmp(opr_name, OPR_EXECUTE_EXIT) == 0) {
		/* Execute From Address a non-return code */

		addr = strtoull(addr_str, &stop_str, 0);

		opr_execute_exit(addr);
		exit_uart_app(EC_OK);
	} else if (strcmp(opr_name, OPR_EXECUTE_CONT) == 0) {
		/* Execute From Address a returnable code */

		addr = strtoull(addr_str, &stop_str, 0);

		opr_execute_return(addr);
	} else {
		exit_uart_app(EC_UNSUPPORTED_CMD_ERR);
	}

	exit_uart_app(EC_OK);
	return 0;
}

/*---------------------------------------------------------------------------
 * Function:	param_parse_cmd_line
 *
 * Parameters:		argc - Argument Count.
 *			argv - Argument Vector.
 * Returns:		None.
 * Side effects:
 * Description:
 *		Parse command line parameters.
 *---------------------------------------------------------------------------
 */

static const struct option long_opts[] = {
	{ "version", 0, 0, 'v' },  { "help", 0, 0, 'h' },
	{ "quiet", 0, 0, 'q' },	   { "console", 0, 0, 'c' },
	{ "auto", 0, 0, 'A' },	   { "read-flash", 0, 0, 'r' },
	{ "baudrate", 1, 0, 'b' }, { "opr", 1, 0, 'o' },
	{ "port", 1, 0, 'p' },	   { "file", 1, 0, 'f' },
	{ "addr", 1, 0, 'a' },	   { "size", 1, 0, 's' },
	{ "offset", 1, 0, 'O' },   { NULL, 0, 0, 0 }
};

static const char *short_opts = "vhqcArb:o:p:f:a:s:O:?";

static void param_parse_cmd_line(int argc, char *argv[])
{
	int opt, idx;

	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &idx)) !=
	       -1) {
		switch (opt) {
		case 'v':
			main_print_version();
			exit(EC_OK);
		case 'h':
		case '?':
			tool_usage();
			opr_usage();
			exit(EC_OK);
		case 'q':
			verbose = false;
			break;
		case 'c':
			console = true;
			break;
		case 'A':
			auto_mode = true;
			break;
		case 'r':
			read_flash_flag = true;
			break;
		case 'b':
			if (sscanf(optarg, "%du", &baudrate) == 0)
				exit(EC_BAUDRATE_ERR);
			break;
		case 'o':
			strncpy(opr_name, optarg, sizeof(opr_name));
			opr_name[sizeof(opr_name) - 1] = '\0';
			break;
		case 'p':
			strncpy(port_name, optarg, sizeof(port_name));
			port_name[sizeof(port_name) - 1] = '\0';
			break;
		case 'f':
			strncpy(file_name, optarg, sizeof(file_name));
			file_name[sizeof(file_name) - 1] = '\0';
			break;
		case 'a':
			strncpy(addr_str, optarg, sizeof(addr_str));
			addr_str[sizeof(addr_str) - 1] = '\0';
			break;
		case 's':
			strncpy(size_str, optarg, sizeof(size_str));
			size_str[sizeof(size_str) - 1] = '\0';
			break;
		case 'O':
			flash_offset = strtol(optarg, NULL, 0);
			break;
		}
	}
}

/*---------------------------------------------------------------------------
 * Function:	param_check_opr_num
 *
 * Parameters:	opr - Operation Number.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Verify the validity of operation Number.
 *---------------------------------------------------------------------------
 */
static void param_check_opr_num(const char *opr)
{
	if ((strcasecmp(opr, OPR_WRITE_MEM) != 0) &&
	    (strcasecmp(opr, OPR_READ_MEM) != 0) &&
	    (strcasecmp(opr, OPR_EXECUTE_EXIT) != 0) &&
	    (strcasecmp(opr, OPR_EXECUTE_CONT) != 0)) {
		display_color_msg(
			FAIL,
			"ERROR: Operation %s not supported, Supported "
			"operations are %s, %s, %s & %s\n",
			opr, OPR_WRITE_MEM, OPR_READ_MEM, OPR_EXECUTE_EXIT,
			OPR_EXECUTE_CONT);
		exit_uart_app(EC_OPR_MUM_ERR);
	}
}

/*---------------------------------------------------------------------------
 * Function:	param_get_file_size
 *
 * Parameters:	file_name - input file name.
 * Returns:	size of file (in bytes).
 * Side effects:
 * Description:
 *		Retrieve the size (in bytes) of a given file.
 *--------------------------------------------------------------------------
 */
static uint32_t param_get_file_size(const char *file_name)
{
	struct stat fst;

	if (stat(file_name, &fst)) {
		display_color_msg(FAIL, "ERROR: Could not stat file [%s]\n",
				  file_name);
		return 0;
	}
	return fst.st_size;
}

/*---------------------------------------------------------------------------
 * Function:	param_get_str_size
 *
 * Parameters:	string - input string.
 * Returns:	size of double-words (in bytes).
 * Side effects:
 * Description:
 *	Retrieve the size (in bytes) of double-word values in a given string.
 *	E.g., given the string "1234 AB5678 FF", return 12 (for three
 *	double-words).
 *---------------------------------------------------------------------------
 */
static uint32_t param_get_str_size(char *string)
{
	uint32_t str_size = 0;
	char seps[] = " ";
	char *token = NULL;

	/* Verify string is non-NULL */
	if ((string == NULL) || (strlen(string) == 0)) {
		display_color_msg(FAIL,
				  "ERROR: Zero length input string provided\n");
		return 0;
	}

	/* Read first token from string */
	token = strtok(string, seps);

	/* Loop while there are tokens in "string" */
	while (token != NULL) {
		str_size++;
		token = strtok(NULL, seps);
	}

	/* Refer to each token as a double-word */
	str_size *= sizeof(uint32_t);
	return str_size;
}

/*--------------------------------------------------------------------------
 * Function:	tool_usage
 *
 * Parameters:	none.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Prints the console application help menu.
 *--------------------------------------------------------------------------
 */
static void tool_usage(void)
{
	printf("%s version %s\n\n", tool_name, tool_version);
	printf("General switches:\n");
	printf("  -v, --version        - Print version\n");
	printf("  -h, --help           - Help menu\n");
	printf("  -q, --quiet          - Suppress verbose mode (default is "
	       "verbose ON)\n");
	printf("  -c, --console        - Print data to console (default is "
	       "print to file)\n");
	printf("  -p, --port <name>    - Serial port name (default is %s)\n",
	       DEFAULT_PORT_NAME);
	printf("  -b, --baudrate <num> - COM Port baud-rate (default is %d)\n",
	       DEFAULT_BAUD_RATE);
	printf("  -A, --auto           - Enable auto mode. (default is off)\n");
	printf("  -O, --offset <num>   - With --auto, assign the offset of");
	printf(" flash where the image to be written.\n");
	printf("  -r, --read-flash     - With --file=<file>, Read the whole"
	       " flash content and write it to <file>.\n");
	printf("\n");
	printf("Operation specific switches:\n");
	printf("  -o, --opr   <name>   - Operation number (see list below)\n");
	printf("  -f, --file  <name>   - Input/output file name\n");
	printf("  -a, --addr  <num>    - Start memory address\n");
	printf("  -s, --size  <num>    - Size of data to read\n");
	printf("\n");
}

/*--------------------------------------------------------------------------
 * Function:	main_print_version
 *
 * Parameters:	none
 * Returns:	none
 * Side effects:
 * Description:
 *		This routine prints the tool version
 *--------------------------------------------------------------------------
 */
static void main_print_version(void)
{
	printf("%s version %s\n\n", tool_name, tool_version);
}

/*---------------------------------------------------------------------------
 * Function:	exit_uart_app
 *
 * Parameters:	none.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Exit "nicely" the application.
 *---------------------------------------------------------------------------
 */
static void exit_uart_app(int32_t exit_status)
{
	if (opr_close_port() != true)
		display_color_msg(FAIL, "ERROR: Port close failed.\n");

	exit(exit_status);
}

/*---------------------------------------------------------------------------
 * Function:	display_color_msg
 *
 * Parameters:
 *		success - SUCCESS for successful message, FAIL for erroneous
 *			  massage.
 *		fmt     - Massage to dispaly (format and arguments).
 *
 * Returns:	none
 * Side effects: Using DISPLAY_MSG macro.
 * Description:
 *		This routine displays a message using color attributes:
 *		In case of a successful message, use green foreground text on
 *		black background.
 *		In case of an erroneous message, use red foreground text on
 *		black background.
 *---------------------------------------------------------------------------
 */
void display_color_msg(bool success, const char *fmt, ...)
{
	va_list argptr;

	va_start(argptr, fmt);
	vprintf(fmt, argptr);
	va_end(argptr);
}
