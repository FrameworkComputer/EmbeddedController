/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hwtimer.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "touchpad.h"
#include "touchpad_st.h"
#include "update_fw.h"
#include "usb_hid_touchpad.h"
#include "util.h"

/* Console output macros */
#define CC_TOUCHPAD CC_USB
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ## args)

#define SPI (&(spi_devices[SPI_ST_TP_DEVICE_ID]))

BUILD_ASSERT(sizeof(struct st_tp_event_t) == 8);

static struct st_tp_system_info_t system_info;

static int st_tp_read_all_events(void);
static int st_tp_send_ack(void);
static int st_tp_start_scan(void);
static int st_tp_stop_scan(void);
static int st_tp_read_host_buffer_header(void);

/*
 * Current system state, meaning of each bit is defined below.
 */
static int system_state;

#define SYSTEM_STATE_DEBUG_MODE		(1 << 0)
#define SYSTEM_STATE_ENABLE_HEAT_MAP	(1 << 1)
#define SYSTEM_STATE_ENABLE_DOME_SWITCH	(1 << 2)
#define SYSTEM_STATE_ACTIVE_MODE	(1 << 3)
#define SYSTEM_STATE_DOME_SWITCH_LEVEL  (1 << 4)

/*
 * Timestamp of last interrupt (32 bits are enough as we divide the value by 100
 * and then put it in a 16-bit field).
 */
static uint32_t irq_ts;

static struct {
#if ST_TP_DUMMY_BYTE == 1
	uint8_t dummy;
#endif
	union {
		uint8_t bytes[512];
		struct st_tp_host_buffer_header_t buffer_header;
		struct st_tp_host_buffer_heat_map_t heat_map;
		struct st_tp_host_data_header_t data_header;
		struct st_tp_event_t events[32];
	} /* anonymous */;
} __packed rx_buf;

static void set_bits(int *lvalue, int rvalue, int mask)
{
	*lvalue &= ~mask;
	*lvalue |= rvalue & mask;
}

/*
 * Parse a finger report from ST event and save it to (report)->finger.
 *
 * @param report: pointer to a USB HID touchpad report.
 * @param event: a pointer event from ST.
 * @param i: array index for next finger.
 *
 * @return array index of next finger (i.e. (i + 1) if a finger is added).
 */
static int st_tp_parse_finger(struct usb_hid_touchpad_report *report,
			      struct st_tp_event_t *event,
			      int i)
{
	/* We cannot report more fingers */
	if (i >= ARRAY_SIZE(report->finger))
		return i;

	/* This is not a finger */
	if (event->finger.touch_type == ST_TP_TOUCH_TYPE_INVALID)
		return i;

	switch (event->evt_id) {
	case ST_TP_EVENT_ID_ENTER_POINTER:
	case ST_TP_EVENT_ID_MOTION_POINTER:
		report->finger[i].tip = 1;
		report->finger[i].inrange = 1;
		report->finger[i].id = event->finger.touch_id;
		report->finger[i].pressure = event->finger.z;
		report->finger[i].width = (event->finger.minor |
					   (event->minor_high << 4));
		report->finger[i].height = (event->finger.major |
					    (event->major_high << 4));
		report->finger[i].x = (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X -
				       event->finger.x);
		report->finger[i].y = (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y -
				       event->finger.y);
		break;
	case ST_TP_EVENT_ID_LEAVE_POINTER:
		report->finger[i].id = event->finger.touch_id;
		break;
	}
	return i + 1;
}

static int st_tp_write_hid_report(void)
{
	int ret, i, num_finger;
	struct usb_hid_touchpad_report report;
	struct st_tp_event_t *event;

	ret = st_tp_read_host_buffer_header();
	if (ret)
		return ret;

	if (rx_buf.buffer_header.flags & ST_TP_BUFFER_HEADER_DOMESWITCH_CHG) {
		/*
		 * dome_switch_level from device is inverted.
		 * That is, 0 => pressed, 1 => released.
		 */
		set_bits(&system_state,
			 (rx_buf.buffer_header.dome_switch_level ?
			  0 : SYSTEM_STATE_DOME_SWITCH_LEVEL),
			 SYSTEM_STATE_DOME_SWITCH_LEVEL);
	}

	ret = st_tp_read_all_events();
	if (ret)
		return ret;

	memset(&report, 0, sizeof(report));
	report.id = 0x1;
	num_finger = 0;

	for (i = 0; i < ARRAY_SIZE(rx_buf.events); i++) {
		event = &rx_buf.events[i];

		/*
		 * this is not a valid event, and assume all following
		 * events are invalid too
		 */
		if (event->magic != 0x3)
			break;

		switch (event->evt_id) {
		case ST_TP_EVENT_ID_ENTER_POINTER:
		case ST_TP_EVENT_ID_MOTION_POINTER:
		case ST_TP_EVENT_ID_LEAVE_POINTER:
			num_finger = st_tp_parse_finger(&report,
							event,
							num_finger);
			break;
		default:
			break;
		}
	}

	report.button = !!(system_state & SYSTEM_STATE_DOME_SWITCH_LEVEL);
	report.count = num_finger;
	report.timestamp = irq_ts / USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

	set_touchpad_report(&report);
	return ret;
}

static int st_tp_read_report(void)
{
	if (system_state & SYSTEM_STATE_ENABLE_HEAT_MAP) {
		/* TODO(stimim): implement this */
	} else {
		st_tp_write_hid_report();
	}
	return st_tp_send_ack();
}

static int st_tp_read_host_buffer_header(void)
{
	const uint8_t tx_buf[] = { ST_TP_CMD_READ_SPI_HOST_BUFFER, 0x00, 0x00 };
	int rx_len = ST_TP_DUMMY_BYTE + sizeof(rx_buf.buffer_header);

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf),
			       (uint8_t *)&rx_buf, rx_len);
}

static int st_tp_send_ack(void)
{
	uint8_t tx_buf[] = { ST_TP_CMD_SPI_HOST_BUFFER_ACK };

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int st_tp_update_system_state(int new_state, int mask)
{
	int ret = EC_SUCCESS;

	/* copy reserved bits */
	set_bits(&new_state, system_state, ~mask);

	mask = SYSTEM_STATE_DEBUG_MODE;
	if ((new_state & mask) != (system_state & mask))
		set_bits(&system_state, new_state, mask);

	mask = SYSTEM_STATE_ENABLE_HEAT_MAP | SYSTEM_STATE_ENABLE_DOME_SWITCH;
	if ((new_state & mask) != (system_state & mask)) {
		uint8_t tx_buf[] = {
			ST_TP_CMD_WRITE_FEATURE_SELECT,
			0x05,
			0
		};
		if (new_state & SYSTEM_STATE_ENABLE_HEAT_MAP)
			tx_buf[2] |= 1 << 0;
		if (new_state & SYSTEM_STATE_ENABLE_DOME_SWITCH)
			tx_buf[2] |= 1 << 1;
		ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
		if (ret)
			return ret;
		set_bits(&system_state, new_state, mask);
	}

	mask = SYSTEM_STATE_ACTIVE_MODE;
	if ((new_state & mask) != (system_state & mask)) {
		uint8_t tx_buf[] = {
			ST_TP_CMD_WRITE_SCAN_MODE_SELECT,
			ST_TP_SCAN_MODE_ACTIVE,
			!!(new_state & SYSTEM_STATE_ACTIVE_MODE),
		};
		CPRINTS("Enable Multi-Touch: %d", tx_buf[2]);
		ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
		if (ret)
			return ret;
		set_bits(&system_state, new_state, mask);
	}
	return ret;
}

static void st_tp_enable_interrupt(int enable)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x01, enable ? 1 : 0};
	if (enable)
		gpio_enable_interrupt(GPIO_TOUCHPAD_INT);
	spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
	if (!enable)
		gpio_disable_interrupt(GPIO_TOUCHPAD_INT);
}

static int st_tp_start_scan(void)
{
	int new_state = (SYSTEM_STATE_ACTIVE_MODE |
			 SYSTEM_STATE_ENABLE_DOME_SWITCH);
	int mask = new_state;
	int ret;

	ret = st_tp_update_system_state(new_state, mask);
	if (ret)
		return ret;
	st_tp_send_ack();
	st_tp_enable_interrupt(1);
	return ret;
}

static int st_tp_read_host_data_memory(uint16_t addr, void *rx_buf, int len) {
	uint8_t tx_buf[] = {
		ST_TP_CMD_READ_HOST_DATA_MEMORY, addr >> 8, addr & 0xFF
	};

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), rx_buf, len);
}

static int st_tp_stop_scan(void)
{
	int new_state = 0;
	int mask = SYSTEM_STATE_ACTIVE_MODE;
	int ret;

	ret = st_tp_update_system_state(new_state, mask);
	st_tp_enable_interrupt(0);
	return ret;
}

static int st_tp_load_host_data(uint8_t mem_id)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x06, mem_id
	};
	int retry, ret;
	uint16_t count;
	struct st_tp_host_data_header_t *header = &rx_buf.data_header;
	int rx_len = sizeof(*header) + ST_TP_DUMMY_BYTE;

	st_tp_read_host_data_memory(0x0000, &rx_buf, rx_len);
	if (header->host_data_mem_id == mem_id)
		return EC_SUCCESS; /* already loaded no need to reload */

	count = header->count;

	ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
	if (ret)
		return ret;

	ret = EC_ERROR_TIMEOUT;
	retry = 5;
	while (retry--) {
		st_tp_read_host_data_memory(0x0000, &rx_buf, rx_len);
		if (header->magic == ST_TP_HEADER_MAGIC &&
		    header->host_data_mem_id == mem_id &&
		    header->count != count) {
			ret = EC_SUCCESS;
			break;
		}
		usleep(10 * MSEC);
	}
	return ret;
}

/*
 * Read System Info from Host Data Memory.
 *
 * @param reload: true to force reloading system info into host data memory
 *                before reading.
 */
static int st_tp_read_system_info(int reload)
{
	int ret = EC_SUCCESS;
	int rx_len = ST_TP_DUMMY_BYTE + ST_TP_SYSTEM_INFO_LEN;
	uint8_t *ptr = rx_buf.bytes;

	if (reload)
		ret = st_tp_load_host_data(ST_TP_MEM_ID_SYSTEM_INFO);
	if (ret)
		return ret;
	ret = st_tp_read_host_data_memory(0x0000, &rx_buf, rx_len);
	if (ret)
		return ret;

	/* Parse the content */
	memcpy(&system_info, ptr, ST_TP_SYSTEM_INFO_PART_1_SIZE);

	/* Check header */
	if (system_info.header.magic != ST_TP_HEADER_MAGIC ||
	    system_info.header.host_data_mem_id != ST_TP_MEM_ID_SYSTEM_INFO)
		return EC_ERROR_UNKNOWN;

	ptr += ST_TP_SYSTEM_INFO_PART_1_SIZE;
	ptr += ST_TP_SYSTEM_INFO_PART_1_RESERVED;
	memcpy(&system_info.scr_res_x, ptr, ST_TP_SYSTEM_INFO_PART_2_SIZE);

#define ST_TP_SHOW(attr) CPRINTS(#attr ": %04x", system_info.attr)
	ST_TP_SHOW(chip0_id[0]);
	ST_TP_SHOW(chip0_id[1]);
	ST_TP_SHOW(chip0_ver);
	ST_TP_SHOW(scr_tx_len);
	ST_TP_SHOW(scr_rx_len);
	ST_TP_SHOW(release_info);
#undef ST_TP_SHOW
	return ret;
}

static int st_tp_read_all_events(void)
{
	uint8_t cmd = ST_TP_CMD_READ_ALL_EVENTS;
	int rx_len = sizeof(rx_buf.events) + ST_TP_DUMMY_BYTE;

	return spi_transaction(SPI, &cmd, 1, (uint8_t *)&rx_buf, rx_len);
}

static int st_tp_reset(void)
{
	board_touchpad_reset();
	return st_tp_read_all_events();
}

/* Initialize the controller ICs after reset */
static void st_tp_init(void)
{
	st_tp_reset();
	/*
	 * On boot, ST firmware will load system info to host data memory,
	 * So we don't need to reload it.
	 */
	st_tp_read_system_info(0);

	system_state = 0;

	st_tp_start_scan();
}
DECLARE_DEFERRED(st_tp_init);

#ifdef CONFIG_USB_UPDATE
int touchpad_get_info(struct touchpad_info *tp)
{
	if (st_tp_read_system_info(1)) {
		tp->status = EC_RES_SUCCESS;
		tp->vendor = ST_VENDOR_ID;
		/*
		 * failed to get system info, FW corrupted, return some default
		 * values.
		 */
		tp->st.id = 0x3936;
		tp->st.fw_version = 0;
		tp->st.fw_checksum = 0;
		return sizeof(*tp);
	}

	tp->status = EC_RES_SUCCESS;
	tp->vendor = ST_VENDOR_ID;
	tp->st.id = (system_info.chip0_id[0] << 8) | system_info.chip0_id[1];
	tp->st.fw_version = system_info.release_info;
	tp->st.fw_checksum = system_info.fw_crc;

	return sizeof(*tp);
}

/*
 * Helper functions for firmware update
 *
 * There is no documentation about ST_TP_CMD_WRITE_HW_REG (0xFA).
 * All implementations below are based on sample code from ST.
 */
static int write_hwreg_cmd32(uint32_t address, uint32_t data)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_WRITE_HW_REG,
		(address >> 24) & 0xFF,
		(address >> 16) & 0xFF,
		(address >> 8) & 0xFF,
		(address >> 0) & 0xFF,
		(data >> 24) & 0xFF,
		(data >> 16) & 0xFF,
		(data >> 8) & 0xFF,
		(data >> 0) & 0xFF,
	};

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int write_hwreg_cmd8(uint32_t address, uint8_t data)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_WRITE_HW_REG,
		(address >> 24) & 0xFF,
		(address >> 16) & 0xFF,
		(address >> 8) & 0xFF,
		(address >> 0) & 0xFF,
		data,
	};

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int wait_for_flash_ready(uint8_t type)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_READ_HW_REG,
		0x20, 0x00, 0x00, type,
	};
	int ret = EC_SUCCESS, retry = 200;

	while (retry--) {
		ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf),
				      (uint8_t *)&rx_buf, 2);
		if (ret == EC_SUCCESS && !(rx_buf.bytes[0] & 0x80))
			break;
		usleep(50 * MSEC);
	}
	return retry >= 0 ? ret : EC_ERROR_TIMEOUT;
}

static int erase_flash(void)
{
	int ret;

	/* Erase everything, except CX */
	ret = write_hwreg_cmd32(0x20000128, 0xFFFFFF83);
	if (ret)
		return ret;
	ret = write_hwreg_cmd8(0x2000006B, 0x00);
	if (ret)
		return ret;
	ret = write_hwreg_cmd8(0x2000006A, 0xA0);
	if (ret)
		return ret;
	return wait_for_flash_ready(0x6A);
}

static int st_tp_prepare_for_update(void)
{
	/* hold m3 */
	write_hwreg_cmd8(0x20000024, 0x01);
	/* unlock flash */
	write_hwreg_cmd8(0x20000025, 0x20);
	/* unlock flash erase */
	write_hwreg_cmd8(0x200000DE, 0x03);
	erase_flash();

	return EC_SUCCESS;
}

static int st_tp_start_flash_dma(void)
{
	int ret;

	ret = write_hwreg_cmd8(0x20000071, 0xC0);
	if (ret)
		return ret;
	ret = wait_for_flash_ready(0x71);
	return ret;
}

static int st_tp_write_one_chunk(const uint8_t *head,
				 uint32_t addr, uint32_t chunk_size)
{
	uint8_t tx_buf[ST_TP_DMA_CHUNK_SIZE + 5];
	uint32_t index = 0;
	int ret;

	index = 0;

	tx_buf[index++] = ST_TP_CMD_WRITE_HW_REG;
	tx_buf[index++] = (addr >> 24) & 0xFF;
	tx_buf[index++] = (addr >> 16) & 0xFF;
	tx_buf[index++] = (addr >> 8) & 0xFF;
	tx_buf[index++] = (addr >> 0) & 0xFF;
	memcpy(tx_buf + index, head, chunk_size);
	ret = spi_transaction(SPI, tx_buf, chunk_size + 5, NULL, 0);

	return ret;
}

/*
 * @param offset: offset in memory to copy the data (in bytes).
 * @param size: length of data (in bytes).
 * @param data: pointer to data bytes.
 */
static int st_tp_write_flash(int offset, int size, const uint8_t *data)
{
	uint8_t tx_buf[12] = {0};
	const uint8_t *head = data, *tail = data + size;
	uint32_t addr, index, chunk_size;
	uint32_t flash_buffer_size;
	int ret;

	offset >>= 2;  /* offset should be count in words */
	/*
	 * To write to flash, the data has to be separated into several chunks.
	 * Each chunk will be no more than `ST_TP_DMA_CHUNK_SIZE` bytes.
	 * The chunks will first be saved into a buffer, the buffer can only
	 * holds `ST_TP_FLASH_BUFFER_SIZE` bytes.  We have to flush the buffer
	 * when the capacity is reached.
	 */
	while (head < tail) {
		addr = 0x00100000;
		flash_buffer_size = 0;
		while (flash_buffer_size < ST_TP_FLASH_BUFFER_SIZE) {
			chunk_size = MIN(ST_TP_DMA_CHUNK_SIZE, tail - head);
			ret = st_tp_write_one_chunk(head, addr, chunk_size);
			if (ret)
				return ret;

			flash_buffer_size += chunk_size;
			addr += chunk_size;
			head += chunk_size;

			if (head >= tail)
				break;
		}

		/* configuring the DMA */
		flash_buffer_size = flash_buffer_size / 4 - 1;
		index = 0;

		tx_buf[index++] = ST_TP_CMD_WRITE_HW_REG;
		tx_buf[index++] = 0x20;
		tx_buf[index++] = 0x00;
		tx_buf[index++] = 0x00;
		tx_buf[index++] = 0x72;  /* flash DMA config */
		tx_buf[index++] = 0x00;
		tx_buf[index++] = 0x00;

		tx_buf[index++] = offset & 0xFF;
		tx_buf[index++] = (offset >> 8) & 0xFF;
		tx_buf[index++] = flash_buffer_size & 0xFF;
		tx_buf[index++] = (flash_buffer_size >> 8) & 0xFF;
		tx_buf[index++] = 0x00;

		ret = spi_transaction(SPI, tx_buf, index, NULL, 0);
		if (ret)
			return ret;
		ret = st_tp_start_flash_dma();
		if (ret)
			return ret;

		offset += ST_TP_FLASH_BUFFER_SIZE / 4;
	}
	return EC_SUCCESS;
}

/*
 * @param offset: should be address between 0 to 1M, aligned with
 *	ST_TP_DMA_CHUNK_SIZE.
 * @param size: length of `data` array.
 * @param data: content of new touchpad firmware.
 */
int touchpad_update_write(int offset, int size, const uint8_t *data)
{
	int ret;
	uint8_t tx_buf[] = { ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x00, 0x03 };

	CPRINTS("%s %08x %d", __func__, offset, size);
	if (offset == 0) {
		/* stop scanning, interrupt, etc... */
		st_tp_stop_scan();

		ret = st_tp_prepare_for_update();
		if (ret)
			return ret;
	}

	if (offset % ST_TP_DMA_CHUNK_SIZE)
		return EC_ERROR_INVAL;

	if (offset >= ST_TP_FLASH_OFFSET_CX &&
	    offset < ST_TP_FLASH_OFFSET_CONFIG)
		/* don't update CX section */
		return EC_SUCCESS;

	ret = st_tp_write_flash(offset, size, data);
	if (ret)
		return ret;

	if (offset + size == CONFIG_TOUCHPAD_VIRTUAL_SIZE) {
		CPRINTS("%s: End update, wait for reset.", __func__);

		board_touchpad_reset();

		/* Full panel initialization */
		spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);

		hook_call_deferred(&st_tp_init_data, 10 * MSEC);
	}

	return EC_SUCCESS;
}

int touchpad_debug(const uint8_t *param, unsigned int param_size,
		   uint8_t **data, unsigned int *data_size)
{
	return EC_RES_INVALID_COMMAND;
}
#endif

void touchpad_interrupt(enum gpio_signal signal)
{
	irq_ts = __hw_clock_source_read();

	task_wake(TASK_ID_TOUCHPAD);
}

void touchpad_task(void *u)
{
	st_tp_init();

	while (1) {
		task_wait_event(-1);

		while (!gpio_get_level(GPIO_TOUCHPAD_INT))
			st_tp_read_report();
	}
}

/* Debugging commands */
static int command_touchpad_st(int argc, char **argv)
{
	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;
	if (strcasecmp(argv[1], "enable") == 0) {
		return EC_ERROR_NOT_HANDLED;
	} else if (strcasecmp(argv[1], "disable") == 0) {
		return EC_ERROR_NOT_HANDLED;
	} else if (strcasecmp(argv[1], "version") == 0) {
		st_tp_read_system_info(1);
		return 0;
	} else {
		return EC_ERROR_PARAM1;
	}
}
DECLARE_CONSOLE_COMMAND(touchpad_st, command_touchpad_st,
			"<enable|disable|version>",
			"Read write spi. id is spi_devices array index");
