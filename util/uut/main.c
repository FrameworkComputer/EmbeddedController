/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <getopt.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "com_port.h"
#include "main.h"
#include "misc_util.h"
#include "opr.h"

/*----------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */

#define MAX_FILE_NAME_SIZE 512
#define MAX_PARAM_SIZE 32
#define MAX_MSG_SIZE 128

/* Default values */
#define DEFAULT_BAUD_RATE 115200
#define DEFAULT_PORT_NAME "ttyS0"
#define DEFAULT_DEV_NUM 0

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

static const char tool_name[] = {"LINUX UART Update Tool"};
static const char tool_version[] = {"2.0.1"};

static char port_name[MAX_PARAM_SIZE];
static char opr_name[MAX_PARAM_SIZE];
static char file_name[MAX_FILE_NAME_SIZE];
static char addr_str[MAX_PARAM_SIZE];
static char size_str[MAX_PARAM_SIZE];
static uint32_t baudrate;
static uint32_t dev_num;

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
static void exit_uart_app(uint32_t exit_status);

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

	if (argc <= 1)
		exit(EC_UNSUPPORTED_CMD_ERR);

	/* Setup defaults */
	strncpy(port_name, DEFAULT_PORT_NAME, sizeof(port_name));
	baudrate = DEFAULT_BAUD_RATE;
	dev_num = DEFAULT_DEV_NUM;
	opr_name[0] = '\0';
	verbose = true;
	console = false;

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
	sr = opr_check_sync(baudrate);
	if (sr != SR_OK) {
		display_color_msg(FAIL,
			"Host/Device synchronization failed, error = %lu.\n",
			sr);
		exit_uart_app(EC_SYNC_ERR);
	}

	param_check_opr_num(opr_name);

	/* Write buffer data to chosen address */
	if (strcmp(opr_name, OPR_WRITE_MEM) == 0) {
		addr = strtoul(addr_str, &stop_str, 0);

		/*
		 * Copy the input string to an auxiliary buffer, since string
		 * is altered by param_get_str_size
		 */
		memcpy(aux_buf, file_name, sizeof(file_name));

		/* Retrieve input size */
		if (console)
			size = param_get_str_size(aux_buf);
		else
			size = param_get_file_size(file_name);

		/* Ensure non-zero size */
		if (size == 0)
			exit_uart_app(EC_FILE_ERR);

		opr_write_mem(file_name, addr, size);
	} else if (strcmp(opr_name, OPR_READ_MEM) == 0) {
		/* Read data to chosen address */

		addr = strtoul(addr_str, &stop_str, 0);
		size = strtoul(size_str, &stop_str, 0);

		opr_read_mem(file_name, addr, size);
	} else if (strcmp(opr_name, OPR_EXECUTE_EXIT) == 0) {
		/* Execute From Address a non-return code */

		addr = strtoul(addr_str, &stop_str, 0);

		opr_execute_exit(addr);
		exit_uart_app(EC_OK);
	} else if (strcmp(opr_name, OPR_EXECUTE_CONT) == 0) {
		/* Execute From Address a returnable code */

		addr = strtoul(addr_str, &stop_str, 0);

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
	{"version",  0, 0, 'v'},
	{"help",     0, 0, 'h'},
	{"quiet",    0, 0, 'q'},
	{"console",  0, 0, 'c'},
	{"baudrate", 1, 0, 'b'},
	{"opr",      1, 0, 'o'},
	{"port",     1, 0, 'p'},
	{"file",     1, 0, 'f'},
	{"addr",     1, 0, 'a'},
	{"size",     1, 0, 's'},
	{NULL,       0, 0, 0}
};

static char *short_opts = "vhqcb:o:p:f:a:s:?";

static void param_parse_cmd_line(int argc, char *argv[])
{
	int opt, idx;

	while ((opt = getopt_long(argc, argv, short_opts,
				long_opts, &idx)) != -1) {

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
		case 'b':
			if (sscanf(optarg, "%du", &baudrate) == 0)
				exit(EC_BAUDRATE_ERR);
			break;
		case 'o':
			strncpy(opr_name, optarg, sizeof(opr_name));
			opr_name[sizeof(opr_name)-1] = '\0';
			break;
		case 'p':
			strncpy(port_name, optarg, sizeof(port_name));
			port_name[sizeof(port_name)-1] = '\0';
			break;
		case 'f':
			strncpy(file_name, optarg, sizeof(file_name));
			file_name[sizeof(file_name)-1] = '\0';
			break;
		case 'a':
			strncpy(addr_str, optarg, sizeof(addr_str));
			addr_str[sizeof(addr_str)-1] = '\0';
			break;
		case 's':
			strncpy(size_str, optarg, sizeof(size_str));
			size_str[sizeof(size_str)-1] = '\0';
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
		display_color_msg(FAIL,
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
		display_color_msg(FAIL,
				"ERROR: Could not stat file [%s]\n", file_name);
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
			"ERROR: Zero length input string provided\n", string);
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
	printf("      --version         - Print version\n");
	printf("      --help            - Help menu\n");
	printf("      --quiet           - Suppress verbose mode (default is "
	       "verbose ON)\n");
	printf("      --console         - Print data to console (default is "
	       "print to file)\n");
	printf("      --port <name>     - Serial port name (default is %s)\n",
		DEFAULT_PORT_NAME);
	printf("      --baudrate <num>  - COM Port baud-rate (default is %d)\n",
		DEFAULT_BAUD_RATE);
	printf("\n");
	printf("Operation specific switches:\n");
	printf("      --opr   <name>    - Operation number (see list below)\n");
	printf("      --file  <name>    - Input/output file name\n");
	printf("      --addr  <num>     - Start memory address\n");
	printf("      --size  <num>     - Size of data to read\n");
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
static void exit_uart_app(uint32_t exit_status)
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
void display_color_msg(bool success, char *fmt, ...)
{
	va_list argptr;

	va_start(argptr, fmt);
	vprintf(fmt, argptr);
	va_end(argptr);
}
