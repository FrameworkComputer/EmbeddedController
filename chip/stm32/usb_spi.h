/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_SPI_H
#define __CROS_EC_USB_SPI_H

/* STM32 USB SPI driver for Chrome EC */

#include "compile_time_macros.h"
#include "hooks.h"
#include "usb_descriptor.h"
#include "usb_hw.h"

/*
 * This SPI flash programming interface is designed to talk to a Chromium OS
 * device over a Raiden USB connection.
 *
 * USB SPI Version 2:
 *
 *     USB SPI version 2 adds support for larger SPI transfers and reduces the
 *     number of USB packets transferred. This improves performance when
 *     writing or reading large chunks of memory from a device. A packet ID
 *     field is used to distinguish the different packet types. Additional
 *     packets have been included to query the device for its configuration
 *     allowing the interface to be used on platforms with different SPI
 *     limitations. It includes validation and a packet to recover from the
 *     situations where USB packets are lost.
 *
 *     The USB SPI hosts which support packet version 2 are backwards compatible
 *     and use the bInterfaceProtocol field to identify which type of target
 *     they are connected to.
 *
 *
 * Example: USB SPI request with 128 byte write and 0 byte read.
 *
 *      Packet #1 Host to Device:
 *           packet id   = USB_SPI_PKT_ID_CMD_TRANSFER_START
 *           write count = 128
 *           read count  = 0
 *           payload     = First 58 bytes from the write buffer,
 *                            starting at byte 0 in the buffer
 *           packet size = 64 bytes
 *
 *      Packet #2 Host to Device:
 *           packet id   = USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE
 *           data index  = 58
 *           payload     = Next 60 bytes from the write buffer,
 *                           starting at byte 58 in the buffer
 *           packet size = 64 bytes
 *
 *      Packet #3 Host to Device:
 *           packet id   = USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE
 *           data index  = 118
 *           payload     = Next 10 bytes from the write buffer,
 *                           starting at byte 118 in the buffer
 *           packet size = 14 bytes
 *
 *      Packet #4 Device to Host:
 *           packet id   = USB_SPI_PKT_ID_RSP_TRANSFER_START
 *           status code = status code from device
 *           payload     = 0 bytes
 *           packet size = 4 bytes
 *
 * Example: USB SPI request with 2 byte write and 100 byte read.
 *
 *      Packet #1 Host to Device:
 *           packet id   = USB_SPI_PKT_ID_CMD_TRANSFER_START
 *           write count = 2
 *           read count  = 100
 *           payload     = The 2 byte write buffer
 *           packet size = 8 bytes
 *
 *      Packet #2 Device to Host:
 *           packet id   = USB_SPI_PKT_ID_RSP_TRANSFER_START
 *           status code = status code from device
 *           payload     = First 60 bytes from the read buffer,
 *                            starting at byte 0 in the buffer
 *           packet size = 64 bytes
 *
 *      Packet #3 Device to Host:
 *           packet id   = USB_SPI_PKT_ID_RSP_TRANSFER_CONTINUE
 *           data index  = 60
 *           payload     = Next 40 bytes from the read buffer,
 *                           starting at byte 60 in the buffer
 *           packet size = 44 bytes
 *
 *
 * Message Packets:
 *
 * Command Start Packet (Host to Device):
 *
 *      Start of the USB SPI command, contains the number of bytes to write
 *      and read on SPI and up to the first 58 bytes of write payload.
 *      Longer writes will use the continue packets with packet id
 *      USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE to transmit the remaining data.
 *
 *     +----------------+------------------+-----------------+---------------+
 *     | packet id : 2B | write count : 2B | read count : 2B | w.p. : <= 58B |
 *     +----------------+------------------+-----------------+---------------+
 *
 *     packet id:     2 byte enum defined by packet_id_type
 *                    Valid values packet id = USB_SPI_PKT_ID_CMD_TRANSFER_START
 *
 *     write count:   2 byte, zero based count of bytes to write
 *
 *     read count:    2 byte, zero based count of bytes to read
 *                    UINT16_MAX indicates full duplex mode with a read count
 *                    equal to the write count.
 *
 *     write payload: Up to 58 bytes of data to write to SPI, the total
 *                    length of all TX packets must match write count.
 *                    Due to data alignment constraints, this must be an
 *                    even number of bytes unless this is the final packet.
 *
 *
 * Response Start Packet (Device to Host):
 *
 *      Start of the USB SPI response, contains the status code and up to
 *      the first 60 bytes of read payload. Longer reads will use the
 *      continue packets with packet id USB_SPI_PKT_ID_RSP_TRANSFER_CONTINUE
 *      to transmit the remaining data.
 *
 *     +----------------+------------------+-----------------------+
 *     | packet id : 2B | status code : 2B | read payload : <= 60B |
 *     +----------------+------------------+-----------------------+
 *
 *     packet id:     2 byte enum defined by packet_id_type
 *                    Valid values packet id = USB_SPI_PKT_ID_RSP_TRANSFER_START
 *
 *     status code: 2 byte status code
 *         0x0000: Success
 *         0x0001: SPI timeout
 *         0x0002: Busy, try again
 *             This can happen if someone else has acquired the shared memory
 *             buffer that the SPI driver uses as /dev/null
 *         0x0003: Write count invalid. The byte limit is platform specific
 *             and is set during the configure USB SPI response.
 *         0x0004: Read count invalid. The byte limit is platform specific
 *             and is set during the configure USB SPI response.
 *         0x0005: The SPI bridge is disabled.
 *         0x0006: The RX continue packet's data index is invalid. This
 *             can indicate a USB transfer failure to the device.
 *         0x0007: The RX endpoint has received more data than write count.
 *             This can indicate a USB transfer failure to the device.
 *         0x0008: An unexpected packet arrived that the device could not
 *             process.
 *         0x0009: The device does not support full duplex mode.
 *         0x000A: Requested serial flash mode not supported
 *         0x8000: Unknown error mask
 *             The bottom 15 bits will contain the bottom 15 bits from the EC
 *             error code.
 *
 *     read payload: Up to 60 bytes of data read from SPI, the total
 *                   length of all RX packets must match read count
 *                   unless an error status was returned. Due to data
 *                   alignment constraints, this must be a even number
 *                   of bytes unless this is the final packet.
 *
 *
 * Continue Packet (Bidirectional):
 *
 *      Continuation packet for the writes and read buffers. Both packets
 *      follow the same format, a data index counts the number of bytes
 *      previously transferred in the USB SPI transfer and a payload of bytes.
 *
 *     +----------------+-----------------+-------------------------------+
 *     | packet id : 2B | data index : 2B | write / read payload : <= 60B |
 *     +----------------+-----------------+-------------------------------+
 *
 *     packet id:     2 byte enum defined by packet_id_type
 *                    The packet id has 2 values depending on direction:
 *                    packet id = USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE
 *                    indicates the packet is being transmitted from the host
 *                    to the device and contains SPI write payload.
 *                    packet id = USB_SPI_PKT_ID_RSP_TRANSFER_CONTINUE
 *                    indicates the packet is being transmitted from the device
 *                    to the host and contains SPI read payload.
 *
 *     data index:    The data index indicates the number of bytes in the
 *                    read or write buffers that have already been transmitted.
 *                    It is used to validate that no packets have been dropped
 *                    and that the prior packets have been correctly decoded.
 *                    This value corresponds to the offset bytes in the buffer
 *                    to start copying the payload into.
 *
 *     read and write payload:
 *                    Contains up to 60 bytes of payload data to transfer to
 *                    the SPI write buffer or from the SPI read buffer.
 *
 *
 * Command Get Configuration Packet (Host to Device):
 *
 *      Query the device to request it's USB SPI configuration indicating
 *      the number of bytes it can write and read.
 *
 *     +----------------+
 *     | packet id : 2B |
 *     +----------------+
 *
 *     packet id:     2 byte enum USB_SPI_PKT_ID_CMD_GET_USB_SPI_CONFIG
 *
 * Response Configuration Packet (Device to Host):
 *
 *      Response packet form the device to report the maximum write and
 *      read size supported by the device.
 *
 *     +----------------+----------------+---------------+----------------+
 *     | packet id : 2B | max write : 2B | max read : 2B | feature bitmap |
 *     +----------------+----------------+---------------+----------------+
 *
 *     packet id:         2 byte enum USB_SPI_PKT_ID_RSP_USB_SPI_CONFIG
 *
 *     max write count:   2 byte count of the maximum number of bytes
 *                        the device can write to SPI in one transaction.
 *
 *     max read count:    2 byte count of the maximum number of bytes
 *                        the device can read from SPI in one transaction.
 *
 *     feature bitmap:    Bitmap of supported features.
 *                        BIT(0): Full duplex SPI mode is supported
 *                        BIT(1): Serial flash extensions are supported
 *                        BIT(2): Dual mode flash supported
 *                        BIT(3): Quad mode flash supported
 *                        BIT(4): Octo mode flash supported
 *                        BIT(5): Double transfer rate supported
 *                        BIT(6:15): Reserved for future use
 *
 * Command Restart Response Packet (Host to Device):
 *
 *      Command to restart the response transfer from the device. This enables
 *      the host to recover from a lost packet when reading the response
 *      without restarting the SPI transfer.
 *
 *     +----------------+
 *     | packet id : 2B |
 *     +----------------+
 *
 *     packet id:         2 byte enum USB_SPI_PKT_ID_CMD_RESTART_RESPONSE
 *
 * Command chip select Packet (Host to Device):
 *
 *     +----------------+-------------+
 *     | packet id : 2B | action : 2B |
 *     +----------------+-------------+
 *
 *     packet id:         2 byte enum USB_SPI_PKT_ID_CMD_CHIP_SELECT
 *
 *     action:            2 byte, current options:
 *                            0: Deassert chip select
 *                            1: Assert chip select
 *
 * Response chip select Packet (Device to Host):
 *
 *     +----------------+------------------+
 *     | packet id : 2B | status code : 2B |
 *     +----------------+------------------+
 *
 *     packet id:     2 byte enum USB_SPI_PKT_ID_RSP_CHIP_SELECT
 *
 *     status code: 2 byte status code
 *         0x0000: Success
 *         others: Error
 *
 * Flash Command Start Packet (Host to Device):
 *
 *      Start of the USB serial flash SPI command, contains the number of
 *      bytes to write or read on SPI and up to the first 58 bytes of write
 *      payload.  Longer writes will use the continue packets with packet id
 *      USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE to transmit the remaining data.
 *
 *      The reading or writing of the "main" data will be preceded by an
 *      short sequence of opcode, optional address, optional "alternate data",
 *      and optional 'dummy cycles" on the SPI bus.  Flags indicate how many
 *      bytes of each stage to send, and whether to use advanced features such
 *      as dual or quad signal lanes for each stage of the transfer".
 *
 *      The indicated number of opcode, address and alternate bytes will be
 *      the first in the "write payload".  The "count" field will contain the
 *      number of data bytes to be written/read after the opcode, address and
 *      alternate bytes.
 *
 *      This request is only supported if bit 1 of the "feature bitmap"
 *      indicates that serial flash extensions are supported.  Implementations
 *      will further advertise whether they support dual, quad or octo modes, if
 *      none of these are supported, then support for "dummy cycles" is not
 *      guaranteed either, and callers should use one or two bytes of "extra
 *      address data" for dummy cycles, address length can be up to 7 bytes for
 *      this reason.
 *
 *     +----------------+------------+------------+---------------+
 *     | packet id : 2B | count : 2B | flags : 4B | w.p. : <= 56B |
 *     +----------------+------------+------------+---------------+
 *
 *     packet id:     2 byte enum defined by packet_id_type
 *                    Valid values packet id =
 *                    USB_SPI_PKT_ID_CMD_FLASH_TRANSFER_START
 *
 *     count:         2 byte, zero based count of bytes to read or write
 *
 *     flags:         4 byte, flags
 *          bits 0:1  opcode length in bytes (0-3)
 *          bits 2:4  address length in bytes (0-7)
 *          bits 5:6  mode (0: 1-1-1, 1: 1-1-N, 2: 1-N-N, 3: N-N-N)
 *          bits 7:8  width (0: N=1, 1: N=2, 2: N=4, 3: N=8)
 *             bit 9  double transfer rate (in phases marked as N)
 *        bits 10:14  number of dummy cycles (0-31)
 *        bits 15:27  reserved, must be zero
 *            bit 28  write to be preceded by "write enable"
 *            bit 29  write to be followed by polling of JEDEC "busy bit"
 *            bit 30  reserved, must be zero
 *            bit 31  read (0) / write (1)
 *
 *     write payload: Up to 56 bytes of data to write to SPI, the total length
 *                    of all TX packets must match: write enable length (zero or
 *                    one, depending on bit 27) + opcode length + address length
 *                    + count, (the last one only if bit 31 indicates a write
 *                    operation). Due to data alignment constraints, this must
 *                    be an even number of bytes unless this is the final
 *                    packet.
 *
 *
 * USB Error Codes:
 *
 * send_command return codes have the following format:
 *
 *     0x00000:         Status code success.
 *     0x00001-0x0FFFF: Error code returned by the USB SPI device.
 *     0x10001-0x1FFFF: USB SPI Host error codes
 *     0x20001-0x20063  Lower bits store the positive value representation
 *                      of the libusb_error enum. See the libusb documentation:
 *                      http://libusb.sourceforge.net/api-1.0/group__misc.html
 */

#define USB_SPI_FULL_DUPLEX_ENABLED (UINT16_MAX)

#define USB_SPI_PAYLOAD_SIZE_V2_START (58)

#define USB_SPI_PAYLOAD_SIZE_V2_RESPONSE (60)

#define USB_SPI_PAYLOAD_SIZE_V2_CONTINUE (60)

#define USB_SPI_PAYLOAD_SIZE_V2_ERROR (60)

#define USB_SPI_PAYLOAD_SIZE_FLASH_START (56)

#define USB_SPI_MIN_PACKET_SIZE (2)

/*
 * Values used in spi_device_t.usb_flags
 */

/* Is the USB host allowed to operate on SPI device. */
#define USB_SPI_ENABLED BIT(0)
/* Use board specific SPI driver when forwarding to this device. */
#define USB_SPI_CUSTOM_SPI_DEVICE BIT(1)
/* This SPI device supports dual lane mode. */
#define USB_SPI_FLASH_DUAL_SUPPORT BIT(2)
/* This SPI device supports four lane mode. */
#define USB_SPI_FLASH_QUAD_SUPPORT BIT(3)
/* This SPI device supports eight lane mode. */
#define USB_SPI_FLASH_OCTO_SUPPORT BIT(4)
/* This SPI device supports double transfer rate (data on both clock edges). */
#define USB_SPI_FLASH_DTR_SUPPORT BIT(5)
/*
 * Whether board specific SPI driver supports full duplex.  For SPI devices not
 * using board-specific driver (most devices), this bit has no meaning, as
 * whether full duplex is supported or not is controlled through
 * CONFIG_SPI_HALFDUPLEX.
 */
#define USB_SPI_CUSTOM_SPI_DEVICE_FULL_DUPLEX_SUPPORTED BIT(6)

enum packet_id_type {
	/* Request USB SPI configuration data from device. */
	USB_SPI_PKT_ID_CMD_GET_USB_SPI_CONFIG = 0,
	/* USB SPI configuration data from device. */
	USB_SPI_PKT_ID_RSP_USB_SPI_CONFIG = 1,
	/*
	 * Start a USB SPI transfer specifying number of bytes to write,
	 * read and deliver first packet of data to write.
	 */
	USB_SPI_PKT_ID_CMD_TRANSFER_START = 2,
	/* Additional packets containing write payload. */
	USB_SPI_PKT_ID_CMD_TRANSFER_CONTINUE = 3,
	/*
	 * Request the device restart the response enabling us to recover
	 * from packet loss without another SPI transfer.
	 */
	USB_SPI_PKT_ID_CMD_RESTART_RESPONSE = 4,
	/*
	 * First packet of USB SPI response with the status code
	 * and read payload if it was successful.
	 */
	USB_SPI_PKT_ID_RSP_TRANSFER_START = 5,
	/* Additional packets containing read payload. */
	USB_SPI_PKT_ID_RSP_TRANSFER_CONTINUE = 6,
	/*
	 * Request assertion or deassertion of chip select
	 */
	USB_SPI_PKT_ID_CMD_CHIP_SELECT = 7,
	/* Response to above request. */
	USB_SPI_PKT_ID_RSP_CHIP_SELECT = 8,
	/*
	 * Start a USB serial flash SPI transfer.
	 */
	USB_SPI_PKT_ID_CMD_FLASH_TRANSFER_START = 9,
};

enum feature_bitmap {
	/* Indicates the platform supports full duplex mode. */
	USB_SPI_FEATURE_FULL_DUPLEX_SUPPORTED = BIT(0),
	/* Indicates support for USB_SPI_PKT_ID_CMD_FLASH_TRANSFER_START. */
	USB_SPI_FEATURE_FLASH_EXTENSIONS = BIT(1),
	/*
	 * Indicates that chip and any MUXes support bidirectional data on the
	 * two SPI data lines.
	 */
	USB_SPI_FEATURE_DUAL_MODE_SUPPORTED = BIT(2),
	/*
	 * Indicates that chip and any MUXes support bidirectional data on the
	 * "hold" and "write protect" lines.
	 */
	USB_SPI_FEATURE_QUAD_MODE_SUPPORTED = BIT(3),
	/* Indicates support for eight-line bidirectional data. */
	USB_SPI_FEATURE_OCTO_MODE_SUPPORTED = BIT(4),
	/*
	 * Indicates support for double transfer rate, i.e. data bit shift on
	 * both rising and falling clock edges.
	 */
	USB_SPI_FEATURE_DTR_SUPPORTED = BIT(5),
};

struct usb_spi_response_configuration_v2 {
	uint16_t packet_id;
	uint16_t max_write_count;
	uint16_t max_read_count;
	uint16_t feature_bitmap;
} __packed;

struct usb_spi_command_v2 {
	uint16_t packet_id;
	uint16_t write_count;
	/* UINT16_MAX Indicates readback all on halfduplex compliant devices. */
	uint16_t read_count;
	uint8_t data[USB_SPI_PAYLOAD_SIZE_V2_START];
} __packed;

struct usb_spi_response_v2 {
	uint16_t packet_id;
	uint16_t status_code;
	uint8_t data[USB_SPI_PAYLOAD_SIZE_V2_RESPONSE];
} __packed;

struct usb_spi_continue_v2 {
	uint16_t packet_id;
	uint16_t data_index;
	uint8_t data[USB_SPI_PAYLOAD_SIZE_V2_CONTINUE];
} __packed;

enum chip_select_flags {
	/* Indicates chip select should be asserted. */
	USB_SPI_CHIP_SELECT = BIT(0)
};

struct usb_spi_chip_select_command {
	uint16_t packet_id;
	uint16_t flags;
} __packed;

struct usb_spi_chip_select_response {
	uint16_t packet_id;
	uint16_t status_code;
} __packed;

struct usb_spi_flash_command {
	uint16_t packet_id;
	uint16_t count;
	uint32_t flags;
	uint8_t data[USB_SPI_PAYLOAD_SIZE_FLASH_START];
} __packed;

/*
 * Mask of the flags that are handled by logic in sub_spi.c, and not passed to
 * SPI drivers through usb_spi_board_transaction().
 */
#define FLASH_FLAGS_NONBOARD 0xF0000000UL

#define FLASH_FLAG_WRITE_ENABLE_POS 28U
#define FLASH_FLAG_WRITE_ENABLE (0x1UL << FLASH_FLAG_WRITE_ENABLE_POS)

#define FLASH_FLAG_POLL_POS 29U
#define FLASH_FLAG_POLL (0x1UL << FLASH_FLAG_POLL_POS)

#define FLASH_FLAG_READ_WRITE_POS 31U
#define FLASH_FLAG_READ_WRITE_MSK (0x1UL << FLASH_FLAG_READ_WRITE_POS)
#define FLASH_FLAG_READ_WRITE_READ 0
#define FLASH_FLAG_READ_WRITE_WRITE (0x1UL << FLASH_FLAG_READ_WRITE_POS)

struct usb_spi_packet_ctx {
	union {
		uint8_t bytes[USB_MAX_PACKET_SIZE];
		uint16_t packet_id;
		struct usb_spi_command_v2 cmd_start;
		struct usb_spi_flash_command cmd_flash_start;
		struct usb_spi_continue_v2 cmd_continue;
		struct usb_spi_response_configuration_v2 rsp_config;
		struct usb_spi_response_v2 rsp_start;
		struct usb_spi_continue_v2 rsp_continue;
		struct usb_spi_chip_select_command cmd_cs;
		struct usb_spi_chip_select_response rsp_cs;
	} __packed;
	/*
	 * By storing the number of bytes in the header and knowing that the
	 * USB data packets are all 64B long, we are able to use the header
	 * size to store the offset of the buffer and it's size without
	 * duplicating variables that can go out of sync.
	 */
	size_t header_size;
	/* Number of bytes in the packet. */
	size_t packet_size;
};

enum usb_spi_error {
	USB_SPI_SUCCESS = 0x0000,
	USB_SPI_TIMEOUT = 0x0001,
	USB_SPI_BUSY = 0x0002,
	USB_SPI_WRITE_COUNT_INVALID = 0x0003,
	USB_SPI_READ_COUNT_INVALID = 0x0004,
	USB_SPI_DISABLED = 0x0005,
	/* The RX continue packet's data index is invalid. */
	USB_SPI_RX_BAD_DATA_INDEX = 0x0006,
	/* The RX endpoint has received more data than write count. */
	USB_SPI_RX_DATA_OVERFLOW = 0x0007,
	/* An unexpected packet arrived on the device. */
	USB_SPI_RX_UNEXPECTED_PACKET = 0x0008,
	/* The device does not support full duplex mode. */
	USB_SPI_UNSUPPORTED_FULL_DUPLEX = 0x0009,
	/* The device does not support dual/quad wire mode. */
	USB_SPI_UNSUPPORTED_FLASH_MODE = 0x000A,
	USB_SPI_UNKNOWN_ERROR = 0x8000,
};

enum usb_spi_request {
	USB_SPI_REQ_ENABLE = 0x0000,
	USB_SPI_REQ_DISABLE = 0x0001,
};

/*
 * To optimize for speed, we want to fill whole packets for each transfer
 * This is done by setting the read and write counts to the payload sizes
 * of the smaller start packet + N * continue packets.
 *
 * If a platform has a small maximum SPI transfer size, it can be optimized
 * by setting these limits to the maximum transfer size.
 */
#ifdef CONFIG_USB_SPI_BUFFER_SIZE
#define USB_SPI_BUFFER_SIZE CONFIG_USB_SPI_BUFFER_SIZE
#else
#define USB_SPI_BUFFER_SIZE \
	(USB_SPI_PAYLOAD_SIZE_V2_START + (4 * USB_SPI_PAYLOAD_SIZE_V2_CONTINUE))
#endif
#define USB_SPI_MAX_WRITE_COUNT USB_SPI_BUFFER_SIZE
#define USB_SPI_MAX_READ_COUNT USB_SPI_BUFFER_SIZE

/* Protocol uses two-byte length fields.  Larger buffer makes no sense. */
BUILD_ASSERT(USB_SPI_BUFFER_SIZE <= 65536);

/*
 * Set the enable state for the USB-SPI bridge.
 *
 * The bridge must be enabled from both the host and device side
 * before the SPI bus is usable.  This allows the bridge to be
 * available for host tools to use without forcing the device to
 * disconnect or disable whatever else might be using the SPI bus.
 */
void usb_spi_enable(int enabled);

/*
 * These functions should be implemented by the board to provide any board
 * specific operations required to enable or disable access to the SPI device.
 */
void usb_spi_board_enable(void);
void usb_spi_board_disable(void);

/*
 * In order to facilitate special SPI busses not covered by standard EC
 * drivers, setting the USB_SPI_CUSTOM_SPI_DEVICE_MASK bit of spi_device->port
 * will cause the USB to SPI forwarding logic to invoke this method rather
 * than the standard spi_transaction_async().
 */
int usb_spi_board_transaction(const struct spi_device_t *spi_device,
			      uint32_t flash_flags, const uint8_t *txdata,
			      int txlen, uint8_t *rxdata, int rxlen);

/*
 * Flags to use in usb_spi_board_transaction_async() for advanced serial flash
 * communication, when supported.
 */

/* Number of bytes of opcode (0-3). */
#define FLASH_FLAG_OPCODE_LEN_POS 0
#define FLASH_FLAG_OPCODE_LEN_MSK (0x3U << FLASH_FLAG_OPCODE_LEN_POS)

/* Number of bytes of address plus additional data bytes (0-7). */
#define FLASH_FLAG_ADDR_LEN_POS 2
#define FLASH_FLAG_ADDR_LEN_MSK (0x7U << FLASH_FLAG_ADDR_LEN_POS)

/* At what stage to switch to multi-lane mode (if any). */
#define FLASH_FLAG_MODE_POS 5
#define FLASH_FLAG_MODE_MSK (0x3U << FLASH_FLAG_MODE_POS)
#define FLASH_FLAG_MODE_111 (0x0U << FLASH_FLAG_MODE_POS)
#define FLASH_FLAG_MODE_11N (0x1U << FLASH_FLAG_MODE_POS)
#define FLASH_FLAG_MODE_1NN (0x2U << FLASH_FLAG_MODE_POS)
#define FLASH_FLAG_MODE_NNN (0x3U << FLASH_FLAG_MODE_POS)

/* Data width during the later stages (value of N, above). */
#define FLASH_FLAG_WIDTH_POS 7
#define FLASH_FLAG_WIDTH_MSK (0x3U << FLASH_FLAG_WIDTH_POS)
#define FLASH_FLAG_WIDTH_1WIRE (0x0U << FLASH_FLAG_WIDTH_POS)
#define FLASH_FLAG_WIDTH_2WIRE (0x1U << FLASH_FLAG_WIDTH_POS)
#define FLASH_FLAG_WIDTH_4WIRE (0x2U << FLASH_FLAG_WIDTH_POS)
#define FLASH_FLAG_WIDTH_8WIRE (0x3U << FLASH_FLAG_WIDTH_POS)

/* Transmit opcode bits at both clock edges in later stages. */
#define FLASH_FLAG_DTR_POS 9
#define FLASH_FLAG_DTR (0x1U << FLASH_FLAG_DTR_POS)

/* Number of dummy clock cycles (0-31). */
#define FLASH_FLAG_DUMMY_CYCLES_POS 10
#define FLASH_FLAG_DUMMY_CYCLES_MSK (0x1FU << FLASH_FLAG_DUMMY_CYCLES_POS)

/*
 * Mask of the flags that cannot be ignored.  This is basically any flags
 * which call for wires to switch direction, or data being clocked on both
 * rising and falling edges.  As long as none of these are present, then the
 * remaining flags specifying the length of opcode/address can be ignored, as
 * the entire data buffer can be transmitted as a sequence of bytes, without
 * the controller knowing which parts are to be interpreted as
 * opcode/address/data.
 */
#define FLASH_FLAGS_REQUIRING_SUPPORT \
	(FLASH_FLAG_MODE_MSK | FLASH_FLAG_DTR | FLASH_FLAG_DUMMY_CYCLES_MSK)

#endif /* __CROS_EC_USB_SPI_H */
