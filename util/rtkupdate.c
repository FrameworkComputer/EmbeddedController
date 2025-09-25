/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Filename: rtkupdate.c         For Chipset: RTK EC
 *
 * Function: RTK Flash Utility
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define TOOL_VERSION 0.13
#define SUB_VERSION 0.2

#define BAUDRATE B115200
/* UART sync data */
#define UART_SYNC_BYTE 0x5A
#define UART_SYNC_RESPONSE 0xA5
/* The magic number in packet header */
#define MAGIC_NUMBER_0 0x4B
#define MAGIC_NUMBER_1 0x54
#define MAGIC_NUMBER_2 0x52
#define MAGIC_NUMBER_3 0x43
/* Tell EC which SRAM address the UART data should be stored at */
#define SRAM_BASE_ADDRESS 0x20020000
#define SRAM_CMD_BASE_ADDRESS 0x2005F000
#define UPLOAD_HEADER_SRAM_ADDRESS 0x20010000
#define FRAME_SRAM_BASE_ADDRESS 0x20010020
/* Tell EC which function pointer address to execute next */
#define UPLOAD_FUNCTION_POINTER 0x20010020
/* Retry count */
#define SYNC_RETRY_CNT 3
#define FLASH_RETRY_CNT 2
/* Timeout value */
#define RESPONSE_TIMEOUT 5 /* in seconds */
#define FRAME_RESPONSE_TIMEOUT 5 /* in seconds */
#define WP_RESPONSE_TIMEOUT 2 /* in seconds */
#define OPR_TIMEOUT 10L /* in seconds */
#define WAIT_INTERVAL 100 /* in milliseconds */
/* Debug label */
#define TOOL_DBG 0
#define PRINT_RECEIVED_UNEXPECTED_DATA_ERR 1

#define SPI_INCREMENT 0x1000 /* Address increment per round */
#define PACKET_HEADER_LENGTH 6 /* Length of packet header */
#define CHECKSUM_LENGTH 2 /* Length of checksum */
#define DEFAULT_VALUE 0x00 /* Default value in packet */
#define PAGE_SIZE 256 /* 256 bytes per page */
#define PAGES_PER_ROUND 16 /* 16 pages per round */
#define MAX_PACKET_A_SIZE (PACKET_HEADER_LENGTH + PAGE_SIZE + CHECKSUM_LENGTH)
#define RETRY_COUNT_FOR_SEND_PAGES 10

/* Command type opcode */
enum command_type {
	SUCCESS_PROGRAM_TO_FLASH = 0x03,
	SUCCESS_READ_FROM_FLASH = 0x05,
	START_FRAME_TO_WRITE_TO_FLASH = 0x06,
	WRITE_DATA_TO_SRAM = 0x09,
	WP_COMMAND = 0x0C /* Write protect command */
};

#define TTY_CHANGED 1
#define TTY_NO_CHANGE 0

#if TOOL_DBG
#define DBG_PRINT(format, args...) fprintf(stderr, format, ##args)
#else
#define DBG_PRINT(format, args...)
#endif

#if PRINT_RECEIVED_UNEXPECTED_DATA_ERR
#define ERR_PRINT(format, args...) fprintf(stderr, format, ##args)
#else
#define ERR_PRINT(format, args...)
#endif

static struct termios saved_tty;
static uint8_t tty_changed = TTY_NO_CHANGE;
static int g_uart_fd;

/* Function to calculate Tool-side checksum (sum of all bytes) */
uint16_t calculate_checksum_tool(const unsigned char *data, size_t length)
{
	uint16_t checksum_chk = 0;
	for (size_t i = 0; i < length; i++) {
		checksum_chk += data[i]; /* Sum each byte */
	}
	return checksum_chk;
}

/* Restore UART settings if we ever changed them */
void restore_uart(int fd)
{
	if (tty_changed) {
		tcsetattr(fd, TCSANOW, &saved_tty);
		tty_changed = TTY_NO_CHANGE;
	}
}

/* Function: Set up UART */
int configure_uart(int fd)
{
	struct termios tty;
	speed_t baudrate;

	/* store original setting*/
	if (tcgetattr(fd, &saved_tty) != 0) {
		perror("tcgetattr failed");
		return -1;
	}

	tty = saved_tty;
	tty_changed = TTY_CHANGED;

	/* Set baud rate */
	baudrate = BAUDRATE;
	cfsetospeed(&tty, baudrate);
	cfsetispeed(&tty, baudrate);
	tty.c_cflag |= baudrate;
	tty.c_cflag |= CS8;

	/* Enable software flow control (XON/XOFF) for pseudoterminals */
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
			 ICRNL | IXON);

	/* Set raw mode */
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	/* Disable output processing */
	tty.c_oflag &= ~OPOST;

	/* Set read timeout: 0.5 second */
	tty.c_cc[VTIME] = 5;
	tty.c_cc[VMIN] = 1;
	tty.c_cflag |= (CLOCAL | CREAD); /* ignore modem controls */

	/* enable reading */
	tty.c_cflag &= ~(PARENB | PARODD); /* shut off parity */
	tcflush(fd, TCIFLUSH);

	/* Apply settings */
	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		perror("tcsetattr failed");
		tty_changed = TTY_NO_CHANGE;
		return -1;
	}

	return 0;
}

/* Function: Read file wrapper function for specific bytes */
int read_exact(int fd, unsigned char *buf, size_t count, int timeout_ms)
{
	size_t total_read = 0;
	while (total_read < count) {
		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLIN;
		int ret = poll(&fds, 1, timeout_ms);

		if (ret < 0) {
			DBG_PRINT("poll failed\n");
			return -1;
		}
		if (ret == 0) {
			DBG_PRINT("timed out\n");
			return -1;
		}

		size_t bytes_read =
			read(fd, buf + total_read, count - total_read);
		if (bytes_read <= 0) {
			DBG_PRINT("read failed\n");
			return -1;
		}
		total_read += bytes_read;
	}
	return 0;
}

/* Function: UART synchronization */
int uart_sync(int uart_fd)
{
	unsigned char sync_send = UART_SYNC_BYTE;
	unsigned char sync_receive;
	int retry = SYNC_RETRY_CNT;

	printf("UART_SYNC operation initiated\n");
	tcflush(uart_fd, TCIOFLUSH);

	while (retry--) {
		/* Send sync byte */
		if (write(uart_fd, &sync_send, 1) != 1) {
			perror("Failed to send sync byte");
			return 1;
		}
		DBG_PRINT("Sent sync byte: 0x%X\n", sync_send);

		/* Wait for response */
		int ret = read_exact(uart_fd, &sync_receive, 1, 1000);
		if (ret == 0) {
			DBG_PRINT("Received response: 0x%X\n", sync_receive);
			if (sync_receive == UART_SYNC_RESPONSE) {
				printf("UART sync successful\n");
				return 0; /* Sync successful */
			} else {
				DBG_PRINT(
					"Unexpected response: 0x%X (expected 0xA5)\n",
					sync_receive);
			}
		} else {
			DBG_PRINT("Failed to read sync response");
		}

		/* Retry if not successful */
		if (retry > 0) {
			DBG_PRINT("Retrying UART sync... (%d attempts left)\n",
				  retry);
			sleep(1); /* Wait 1 second before retrying */
		}
	}

	fprintf(stderr, "UART sync failed after 3 attempts\n");
	return 1; /* Sync failed */
}

/* Function: Build and send upload header packet */
int send_upload_header(int uart_fd, uint32_t sram_address, uint32_t spi_address,
		       uint32_t data_size_to_write)
{
	unsigned char packet[24];

	/* Construct packet (upload header command is fixed at 0x09) */
	packet[0] = WRITE_DATA_TO_SRAM;
	packet[1] = 0x0F;

	/* Header address */
	packet[2] = (sram_address >> 24) & 0xFF;
	packet[3] = (sram_address >> 16) & 0xFF;
	packet[4] = (sram_address >> 8) & 0xFF;
	packet[5] = sram_address & 0xFF;

	/* Magic number is fixed at 0x4B, 0x54, 0x52, 0x43 */
	packet[6] = MAGIC_NUMBER_0;
	packet[7] = MAGIC_NUMBER_1;
	packet[8] = MAGIC_NUMBER_2;
	packet[9] = MAGIC_NUMBER_3;

	/* Data size to write */
	packet[10] = data_size_to_write & 0xFF;
	packet[11] = (data_size_to_write >> 8) & 0xFF;
	packet[12] = (data_size_to_write >> 16) & 0xFF;
	packet[13] = (data_size_to_write >> 24) & 0xFF;

	/* Content address: 0x20020000 */
	packet[14] = 0x00;
	packet[15] = 0x00;
	packet[16] = 0x02;
	packet[17] = 0x20;

	/* SPI address */
	packet[18] = spi_address & 0xFF;
	packet[19] = (spi_address >> 8) & 0xFF;
	packet[20] = (spi_address >> 16) & 0xFF;
	packet[21] = (spi_address >> 24) & 0xFF;

	/* Calculate checksum */
	uint16_t checksum = calculate_checksum_tool(packet, 22);

	/* Add checksum */
	packet[22] = (checksum >> 8) & 0xFF;
	packet[23] = checksum & 0xFF;

	/* Send packet */
	ssize_t bytes_written = write(uart_fd, packet, 24);
	if (bytes_written != 24) {
		perror("Failed to send upload header packet");
		return -1;
	}

	DBG_PRINT(
		"Sent Upload Header: SRAM Address=0x%08x, SPI Address=0x%08x, "
		"Data Size to Write = 0x %08x, "
		"checksum = 0x %04x\n ",
		sram_address, spi_address, data_size_to_write, checksum);

	return 0;
}

/* Function: Build and send Packet A (data packet) */
int send_packet_a(int uart_fd, uint8_t command, size_t data_size,
		  uint32_t sram_address, const unsigned char *data)
{
	size_t packet_length =
		PACKET_HEADER_LENGTH + data_size + CHECKSUM_LENGTH;
	unsigned char packet[MAX_PACKET_A_SIZE];

	/* Build packet */
	packet[0] = command;
	packet[1] = (data_size - 1); /* Original data size (off-by-one data)*/
	packet[2] = (sram_address >> 24) & 0xFF;
	packet[3] = (sram_address >> 16) & 0xFF;
	packet[4] = (sram_address >> 8) & 0xFF;
	packet[5] = sram_address & 0xFF;

	/* Copy data into the packet */
	memcpy(&packet[PACKET_HEADER_LENGTH], data, data_size);

	/* Calculate checksum (simple summation) */
	uint16_t checksum = calculate_checksum_tool(
		packet, PACKET_HEADER_LENGTH + data_size);

	/* Add checksum to the end of the packet */
	packet[PACKET_HEADER_LENGTH + data_size] = (checksum >> 8) &
						   0xFF; /* High
							    byte
							  */
	packet[PACKET_HEADER_LENGTH + data_size + 1] = checksum & 0xFF; /* Low
									   byte
									 */

	/* Send the packet */
	ssize_t bytes_written = write(uart_fd, packet, packet_length);
	if (bytes_written != packet_length) {
		perror("Failed to send Packet A");
		return -1;
	}

	DBG_PRINT("Sent Packet A: Command=0x%X, Data Size=%zu, "
		  "SRAM Address = 0x %08X, "
		  "checksum = 0x %04X\n ",
		  command, data_size, sram_address, checksum);

	return 0;
}

/* Function: Build and send Packet B */
int send_packet_b(int uart_fd, uint8_t command, uint32_t func_pointer)
{
	size_t packet_length = PACKET_HEADER_LENGTH + CHECKSUM_LENGTH;
	unsigned char packet[8];

	packet[0] = command;
	packet[1] = DEFAULT_VALUE; /* Fixed default value */
	packet[2] = (func_pointer >> 24) & 0xFF;
	packet[3] = (func_pointer >> 16) & 0xFF;
	packet[4] = (func_pointer >> 8) & 0xFF;
	packet[5] = func_pointer & 0xFF;

	/* Calculate checksum */
	uint16_t checksum =
		calculate_checksum_tool(packet, PACKET_HEADER_LENGTH);
	packet[PACKET_HEADER_LENGTH] = (checksum >> 8) & 0xFF;
	packet[PACKET_HEADER_LENGTH + 1] = checksum & 0xFF;

	/* Send the packet */
	ssize_t bytes_written = write(uart_fd, packet, packet_length);
	if (bytes_written != packet_length) {
		perror("Failed to send Packet B");
		return -1;
	}

	DBG_PRINT(
		"Sent Packet B: Command=0x%X, Function Pointer=0x%08X, checksum=0x%04X\n",
		command, func_pointer, checksum);

	return 0;
}

/* Function: Wait for EC response with timeout */
int wait_for_response(int uart_fd, uint8_t expected_response,
		      int timeout_seconds)
{
	unsigned char response;
	int ret;

	DBG_PRINT("starting read\n");
	ret = read_exact(uart_fd, &response, 1, timeout_seconds * 1000);
	DBG_PRINT("finish read\n");
	if (ret == 0) {
		if (response == expected_response) {
			DBG_PRINT("Received response: 0x%X\n", response);
			return 0; /* Success */
		} else {
			ERR_PRINT(
				"\nUnexpected response: 0x%X (expected: 0x%X)\n",
				response, expected_response);
			return -1;
		}
	}

	fprintf(stderr, "waiting for response fail, need data: 0x%X\n",
		expected_response);

	return -1;
}

/* Function: Send a block of pages */
int send_pages(int uart_fd, FILE *file, uint32_t sram_address,
	       size_t *total_bytes_sent, size_t *page)
{
	unsigned char data_buffer[PAGE_SIZE];
	int retry_count = 0;
	size_t bytes_read = fread(data_buffer, 1, PAGE_SIZE, file);

	if (bytes_read == 0) {
		if (feof(file)) {
			return 0; /* End of file */
		}
		perror("Failed to read from file");
		return -1;
	}

	while (1) {
		retry_count++;
		tcflush(uart_fd, TCIOFLUSH);

		DBG_PRINT("Page %zu, try %d time.\n", *page + 1, retry_count);

		/* Send this page's data */
		if (send_packet_a(uart_fd, WRITE_DATA_TO_SRAM, bytes_read,
				  sram_address, data_buffer) != 0) {
			return -1;
		}

		/* Wait for EC to respond with 0x09
		 * (acknowledgment for this page) */
		if (wait_for_response(uart_fd, WRITE_DATA_TO_SRAM,
				      RESPONSE_TIMEOUT) == 0) {
			break;
		}

		if (retry_count > RETRY_COUNT_FOR_SEND_PAGES) {
			ERR_PRINT(
				"Failed to receive expected response for data page %zu\n",
				*page + 1);
			return -1;
		}

		sleep(1);
	}

	*total_bytes_sent += bytes_read;
	DBG_PRINT("Page %zu sent successfully. Total bytes sent: %zu\n",
		  *page + 1, *total_bytes_sent);
	(*page)++;

	return 0;
}

/* Function: Flash process to send data using Packet A */
int flash(int uart_fd, uint32_t spi_start, const char *file_name)
{
	FILE *file = fopen(file_name, "rb");

	if (!file) {
		perror("Failed to open binary file");
		return -1;
	}

	printf("Flash operation initiated\n");
	size_t total_bytes_sent = 0;
	size_t page = 0;
	uint32_t upload_header_spi_address = spi_start;

	/* Get the total file size */
	fseek(file, 0, SEEK_END);
	size_t total_file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	DBG_PRINT("Total File Size: %zu bytes\n", total_file_size);

	while (total_bytes_sent < total_file_size) {
		printf(".");
		fflush(stdout);
		size_t remaining_data = total_file_size - total_bytes_sent;
		size_t data_size_to_write =
			remaining_data > PAGES_PER_ROUND * PAGE_SIZE ?
				PAGES_PER_ROUND * PAGE_SIZE :
				remaining_data;

		/* Send upload header packet to inform EC of the remaining data
		 * size */
		DBG_PRINT("Sending upload header for new round\n");
		if (send_upload_header(uart_fd, UPLOAD_HEADER_SRAM_ADDRESS,
				       upload_header_spi_address,
				       data_size_to_write) != 0) {
			goto flash_err;
		}

		/* Wait for EC to respond with 0x09 */
		if (wait_for_response(uart_fd, WRITE_DATA_TO_SRAM,
				      RESPONSE_TIMEOUT) != 0) {
			fprintf(stderr,
				"\nFailed to receive expected response for upload header\n");
			goto flash_err;
		}

		for (size_t i = 0;
		     i < PAGES_PER_ROUND && total_bytes_sent < total_file_size;
		     i++) {
			uint32_t sram_address =
				SRAM_BASE_ADDRESS + (i * PAGE_SIZE);

			/* Just send pages to EC's ram */
			if (send_pages(uart_fd, file, sram_address,
				       &total_bytes_sent, &page) != 0) {
				goto flash_err;
			}
		}

		for (uint8_t retry_round = 0; retry_round < FLASH_RETRY_CNT;
		     retry_round++) {
			/* Send Packet B to request EC to move data, even if
			 * this round is not full 16 pages */
			DBG_PRINT(
				"Round %zu complete, sending function pointer to EC.\n",
				page / PAGES_PER_ROUND);
			if (send_packet_b(uart_fd,
					  START_FRAME_TO_WRITE_TO_FLASH,
					  UPLOAD_FUNCTION_POINTER) != 0) {
				goto flash_err;
			}

			/* Wait for EC to respond with 0x06 (acknowledgment for
			 * function pointer) */
			if (wait_for_response(uart_fd, 0x06,
					      RESPONSE_TIMEOUT) != 0) {
				ERR_PRINT(
					"\nFailed to receive expected response (first 0x06)\n");

				if (retry_round < FLASH_RETRY_CNT) {
					continue;
				} else {
					printf("\nFailed to retry request EC to execute frame.\n");
					goto flash_err;
				}
			}
			usleep(100 * 1000);

			/* Wait for EC to respond with 0x06 0x03 (execution
			 * success) */
			unsigned char response[2];
			int ret = read_exact(uart_fd, response, 2, 1000);
			usleep(200 * 1000);
			if (ret != 0) {
				ERR_PRINT(
					"\nexpected 0x06 0x03 response, received: no data\n");
				if (retry_round < FLASH_RETRY_CNT) {
					continue;
				} else {
					goto flash_err;
				}
			}
			if ((response[0] != START_FRAME_TO_WRITE_TO_FLASH) ||
			    (response[1] != SUCCESS_PROGRAM_TO_FLASH)) {
				ERR_PRINT(
					"\nexpected 0x06 0x03 response, received: 0x%X 0x%X\n",
					response[0], response[1]);
				if (retry_round < FLASH_RETRY_CNT) {
					continue;
				} else {
					fprintf(stderr,
						"\n Failed to retry receive frame result.\n");
					goto flash_err;
				}
			}
		}
		/* Update SPI address for next round */
		upload_header_spi_address += SPI_INCREMENT;
		DBG_PRINT("EC successfully processed function pointer.\n");
		/* End loop if this is the final round */
		if (total_bytes_sent >= total_file_size) {
			break;
		}
	}

	printf("\nFlash operation finished.\n");
	fclose(file);
	return 0;

flash_err:
	fclose(file);
	return -1;
}

/* Function: Frame operation */
int frame(int uart_fd, const char *file_name)
{
	printf("Frame operation initiated\n");
	FILE *file = fopen(file_name, "rb");
	if (!file) {
		perror("Failed to open binary file");
		return -1;
	}
	size_t total_bytes_sent = 0;
	size_t page = 0;
	/* Get the total file size */
	fseek(file, 0, SEEK_END);
	size_t total_file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	DBG_PRINT("Total File Size: %zu bytes\n", total_file_size);

	while (total_bytes_sent < total_file_size) {
		for (size_t i = 0; total_bytes_sent < total_file_size; i++) {
			uint32_t sram_address =
				FRAME_SRAM_BASE_ADDRESS + (i * PAGE_SIZE);

			/* Just send pages to EC's ram */
			if (send_pages(uart_fd, file, sram_address,
				       &total_bytes_sent, &page) != 0) {
				goto frame_err;
			}
		}

		DBG_PRINT("EC successfully processed function pointer.\n");
		/* End loop if this is the final round */
		if (total_bytes_sent >= total_file_size) {
			break;
		}
	}

	printf("Frame operation finished.\n");
	fclose(file);
	return 0;

frame_err:
	fclose(file);
	return -1;
}

/* Function: Write protect (WP) operation */
int write_protect(int uart_fd, uint32_t protect)
{
	printf("Write Protect (WP) operation initiated\n");
	unsigned char packet[2];
	packet[0] = WP_COMMAND;
	/* Only using the lower byte of spi_start */
	packet[1] = protect & 0x000000FF;

	DBG_PRINT("write_protect step 1\n");

	/* Send WP command */
	if (write(uart_fd, packet, 2) != 2) {
		perror("Failed to send WP command");
		return -1;
	}

	DBG_PRINT("write_protect step 2\n");
	/* Flush the UART input buffer */
	tcflush(uart_fd, TCIFLUSH);

	DBG_PRINT("write_protect step 3\n");
	/* Wait for expected response from EC */
	if (wait_for_response(uart_fd, WP_COMMAND, WP_RESPONSE_TIMEOUT) != 0) {
		fprintf(stderr,
			"Failed to receive expected response for WP operation\n");
		return -1;
	}
	printf("WP operation successful\n");

	return 0;
}

void out_of_read_bin(int uart_fd)
{
	uint32_t sram_address = SRAM_CMD_BASE_ADDRESS;
	uint8_t data_buffer[4];
	data_buffer[0] = 0x00;
	data_buffer[1] = 0x00;
	data_buffer[2] = 0x00;
	data_buffer[3] = 0x00;

	send_packet_a(uart_fd, WRITE_DATA_TO_SRAM, 4, sram_address,
		      data_buffer);
}

/* Function: Read EC bin operation */
int read_bin(int uart_fd, uint32_t spi_start, const char *file_name,
	     uint32_t bin_length)
{
	printf("Read Bin operation initiated\n");

	FILE *file = fopen(file_name, "w");
	if (!file) {
		perror("Failed to open binary file");
		return -1;
	}
	fseek(file, 0, SEEK_SET);
	unsigned char data_buffer[PAGE_SIZE];
	size_t total_bytes_get = 0;
#if TOOL_DBG
	uint32_t page_read = 0;
#endif
	uint32_t upload_header_spi_address = spi_start;
	uint32_t sram_address = SRAM_CMD_BASE_ADDRESS;

	while (total_bytes_get < bin_length) {
		printf(".");
		fflush(stdout);
		size_t remaining_data = bin_length - total_bytes_get;
		size_t data_size_to_read =
			remaining_data > (PAGES_PER_ROUND * PAGE_SIZE - 1) ?
				(PAGES_PER_ROUND * PAGE_SIZE - 1) :
				remaining_data;

		/* Send upload header packet to inform EC of the remaining data
		 * size */
		DBG_PRINT("Sending upload header for new round\n");
		if (send_upload_header(uart_fd, UPLOAD_HEADER_SRAM_ADDRESS,
				       upload_header_spi_address,
				       data_size_to_read) != 0) {
			goto read_bin_err1;
		}

		/* Wait for EC to respond with 0x09 */
		if (wait_for_response(uart_fd, WRITE_DATA_TO_SRAM,
				      RESPONSE_TIMEOUT) != 0) {
			fprintf(stderr,
				"Failed to receive expected response for upload header\n");
			goto read_bin_err1;
		}
		/* Calculate SRAM address, incremented per page */

		data_buffer[0] = 0xA5;
		data_buffer[1] = 0xA5;
		data_buffer[2] = 0xA5;
		data_buffer[3] = 0xA5;

		/* Send this page's data */
		if (send_packet_a(uart_fd, WRITE_DATA_TO_SRAM, 4, sram_address,
				  data_buffer) != 0) {
			goto read_bin_err2;
		}

		/* Wait for EC to respond with 0x09 (acknowledgment for
		 * this page) */
		if (wait_for_response(uart_fd, WRITE_DATA_TO_SRAM,
				      RESPONSE_TIMEOUT) != 0) {
			DBG_PRINT(
				"Failed to receive expected response for data page %u\n",
				page_read + 1);
		}

		if (send_packet_b(uart_fd, START_FRAME_TO_WRITE_TO_FLASH,
				  UPLOAD_FUNCTION_POINTER) != 0) {
			goto read_bin_err2;
		}

		/* Wait for EC to respond with 0x06 (acknowledgment for function
		 * pointer) */
		if (wait_for_response(uart_fd, 0x06, RESPONSE_TIMEOUT) != 0) {
			ERR_PRINT(
				"\nFailed to receive expected response (first 0x06)\n");
			goto read_bin_err2;
		}
		unsigned char response[PAGES_PER_ROUND * PAGE_SIZE];
		int ret =
			read_exact(uart_fd, response, data_size_to_read, 1000);
		if (ret != 0) {
			DBG_PRINT("read page failed\n");
			out_of_read_bin(uart_fd);
			return -1;
		}

		total_bytes_get += data_size_to_read;
		DBG_PRINT("Page %u sent successfully. Total bytes sent: %zu\n",
			  page_read + 1, total_bytes_get);
#if TOOL_DBG
		page_read += PAGES_PER_ROUND;
#endif
		fwrite(response, 1, data_size_to_read, file);

		/* Wait for EC to respond with 0x06 0x03 (execution success) */
		unsigned char bootrom_response[2];
		ret = read_exact(uart_fd, bootrom_response, 2, 1000);
		usleep(200 * 1000);
		if (ret != 0) {
			ERR_PRINT(
				"\nExpected 0x06 0x05 response, received: no data\n");
			goto read_bin_err2;
		}
		if ((bootrom_response[0] != START_FRAME_TO_WRITE_TO_FLASH) ||
		    (bootrom_response[1] != SUCCESS_READ_FROM_FLASH)) {
			ERR_PRINT(
				"\nExpected 0x06 0x05 response, received: 0x%X 0x%X\n",
				bootrom_response[0], bootrom_response[1]);
			goto read_bin_err2;
		}
		/* Update SPI address for next round */
		upload_header_spi_address += data_size_to_read;
		DBG_PRINT("EC successfully processed function pointer.\n");
		/* End loop if this is the final round */
		if (total_bytes_get >= bin_length) {
			break;
		}
	}
	printf("Read bin operation finished.\n");
	fclose(file);

	data_buffer[0] = 0x00;
	data_buffer[1] = 0x00;
	data_buffer[2] = 0x00;
	data_buffer[3] = 0x00;

	/* Send this page's data */
	if (send_packet_a(uart_fd, WRITE_DATA_TO_SRAM, 4, sram_address,
			  data_buffer) != 0) {
		goto read_bin_err2;
	}

	/* Wait for EC to respond with 0x09 (acknowledgment for
	 * this page) */
	if (wait_for_response(uart_fd, WRITE_DATA_TO_SRAM, RESPONSE_TIMEOUT) !=
	    0) {
		DBG_PRINT(
			"Failed to receive expected response for data page %u\n",
			page_read + 1);
	}

	return 0;

read_bin_err2:
	out_of_read_bin(uart_fd);
read_bin_err1:
	fclose(file);
	return -1;
}

void usage_print(const char *progname)
{
	printf("Usage:\n"
	       "    %s --method flash --uart_dev <dev> [flash options]\n"
	       "    %s --method frame --uart_dev <dev> [frame options]\n"
	       "    %s --method wp --uart_dev <dev> [wp options]\n"
	       "    %s --method read_bin --uart_dev <dev> [read_bin options]\n"
	       "Flash options:\n"
	       "  -s, --spi_start <spi_start>: Specifies the SPI flash offset\n"
	       "  -f, --file <binary_file>: File to program with into flash.\n"
	       "Frame options:\n"
	       "  -f, --file <binary_file>: File to program into RAM.\n"
	       "wp options:\n"
	       "  -p, --protect <0|1>: should be 0 to clear write protect,"
	       " 1 to set write protect.\n"
	       "read_bin options:\n"
	       "  -s, --spi_start <spi_start>: Specifies the SPI flash offset\n"
	       "  -f, --file <binary_file>: File to read from flash.\n"
	       "  -o, --bin_length <bin_length>: Range read from flash.\n",
	       progname, progname, progname, progname);
}

static void sighandler(int signum)
{
	if (g_uart_fd != -1) {
		restore_uart(g_uart_fd);
		close(g_uart_fd);
	}
	exit(128 + signum);
}

static void register_sigaction(void)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
}

int main(int argc, char *argv[])
{
	const char *method = NULL;
	uint32_t spi_start = 0xFFFFFFFF;
	const char *file_name = NULL;
	const char *uart_device = NULL;
	uint32_t bin_length = 0;
	uint32_t protect = 0xFFFFFFFF;
	int opt, para_err = 0;

	static struct option long_options[] = {
		{ "method", required_argument, 0, 'm' },
		{ "spi_start", optional_argument, 0, 's' },
		{ "file", optional_argument, 0, 'f' },
		{ "uart_device", required_argument, 0, 'u' },
		{ "bin_length", optional_argument, 0, 'o' },
		{ "protect", optional_argument, 0, 'p' },
		{ 0, 0, 0, 0 }
	};

	while ((opt = getopt_long(argc, argv, "m:s:f:u:o:p:", long_options,
				  NULL)) != -1) {
		switch (opt) {
		case 'm':
			method = optarg;
			break;
		case 's':
			spi_start = strtoul(optarg, NULL, 0);
			break;
		case 'f':
			file_name = optarg;
			break;
		case 'u':
			uart_device = optarg;
			break;
		case 'o':
			bin_length = strtoul(optarg, NULL, 0);
			break;
		case 'p':
			protect = strtoul(optarg, NULL, 0);
			break;
		default:
			usage_print(argv[0]);
			return 1;
		}
	}

	if (!method) {
		fprintf(stderr, "Missing '--method' arguments\n");
		usage_print(argv[0]);
		return 1;
	} else if (!uart_device) {
		fprintf(stderr, "Missing '--uart_dev' arguments\n");
		usage_print(argv[0]);
		return 1;
	}

	printf("Method: %s\n", method);
	printf("UART Device: %s\n", uart_device);

	/* Open UART device */
	int uart_fd = open(uart_device, O_RDWR | O_NOCTTY);
	g_uart_fd = uart_fd;
	if (uart_fd == -1) {
		perror("Unable to open UART device");
		return 1;
	}

	/* Configure UART settings */
	if (configure_uart(uart_fd) != 0) {
		close(uart_fd);
		return 1;
	}

	/* Perform UART synchronization */
	if (uart_sync(uart_fd) != 0) {
		fprintf(stderr, "UART sync failed\n");
		goto main_err;
	}

	register_sigaction();

	/* Execute method-based operation */
	if (strcmp(method, "flash") == 0) {
		if (!file_name) {
			fprintf(stderr, "Flash missing file_name arguments\n");
			para_err = 1;
		}
		if (spi_start == 0xFFFFFFFF) {
			fprintf(stderr, "Flash missing spi_start arguments\n");
			para_err = 1;
		}
		if (para_err) {
			goto main_err;
		}
		printf("SPI Start: 0x%08X\n", spi_start);
		printf("Binary File: %s\n", file_name);
		if (flash(uart_fd, spi_start, file_name) != 0) {
			fprintf(stderr, "Flash process failed\n");
			goto main_err;
		}
	} else if (strcmp(method, "frame") == 0) {
		if (!file_name) {
			fprintf(stderr, "Frame missing required arguments\n");
			goto main_err;
		}
		printf("Binary File: %s\n", file_name);
		if (frame(uart_fd, file_name) != 0) {
			fprintf(stderr, "Frame process failed\n");
			goto main_err;
		}
	} else if (strcmp(method, "wp") == 0) {
		if (protect == 0xFFFFFFFF) {
			fprintf(stderr, "WP missing required arguments\n");
			goto main_err;
		}
		printf("Write protect: 0x%08X\n", protect);
		if (write_protect(uart_fd, protect) != 0) {
			fprintf(stderr, "WP process failed\n");
			goto main_err;
		}
	} else if (strcmp(method, "read_bin") == 0) {
		if (!bin_length) {
			fprintf(stderr,
				"Read Bin missing bin_length arguments\n");
			para_err = 1;
		}
		if (!file_name) {
			fprintf(stderr,
				"Read Bin missing file_name arguments\n");
			para_err = 1;
		}
		if (spi_start == 0xFFFFFFFF) {
			fprintf(stderr,
				"Read Bin missing spi_start arguments\n");
			para_err = 1;
		}
		if (para_err) {
			goto main_err;
		}
		printf("SPI Start: 0x%08X\n", spi_start);
		printf("Binary File: %s\n", file_name);
		printf("Bin Length: 0x%08X\n", bin_length);
		if (read_bin(uart_fd, spi_start, file_name, bin_length) != 0) {
			fprintf(stderr, "Read Bin failed\n");
			goto main_err;
		}
	} else {
		fprintf(stderr, "Unknown method: %s\n", method);
		goto main_err;
	}

	restore_uart(uart_fd);
	close(uart_fd);
	return 0;

main_err:
	restore_uart(uart_fd);
	close(uart_fd);
	return 1;
}
