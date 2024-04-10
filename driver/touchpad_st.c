/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "printf.h"
#include "registers.h"
#include "spi.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "touchpad.h"
#include "touchpad_st.h"
#include "update_fw.h"
#include "usb_api.h"
#include "usb_hid_touchpad.h"
#include "usb_isochronous.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CC_TOUCHPAD CC_USB
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ##args)

#define TASK_EVENT_POWER TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_TP_UPDATED TASK_EVENT_CUSTOM_BIT(1)

#define SPI (&(spi_devices[SPI_ST_TP_DEVICE_ID]))

BUILD_ASSERT(sizeof(struct st_tp_event_t) == 8);
BUILD_ASSERT(BYTES_PER_PIXEL == 1);

/* Function prototypes */
static int st_tp_panel_init(int full);
static int st_tp_read_all_events(int show_error);
static int st_tp_read_host_buffer_header(void);
static int st_tp_send_ack(void);
static int st_tp_start_scan(void);
static int st_tp_stop_scan(void);
static int st_tp_update_system_state(int new_state, int mask);
static void touchpad_power_control(void);

/* Global variables */
/*
 * Current system state, meaning of each bit is defined below.
 */
static int system_state;

#define SYSTEM_STATE_DEBUG_MODE BIT(0)
#define SYSTEM_STATE_ENABLE_HEAT_MAP BIT(1)
#define SYSTEM_STATE_ENABLE_DOME_SWITCH BIT(2)
#define SYSTEM_STATE_ACTIVE_MODE BIT(3)
#define SYSTEM_STATE_DOME_SWITCH_LEVEL BIT(4)
#define SYSTEM_STATE_READY BIT(5)

/*
 * Pending action for touchpad.
 */
static int tp_control;

#define TP_CONTROL_SHALL_HALT BIT(0)
#define TP_CONTROL_SHALL_RESET BIT(1)
#define TP_CONTROL_SHALL_INIT BIT(2)
#define TP_CONTROL_SHALL_INIT_FULL BIT(3)
#define TP_CONTROL_SHALL_DUMP_ERROR BIT(4)
#define TP_CONTROL_RESETTING BIT(5)
#define TP_CONTROL_INIT BIT(6)
#define TP_CONTROL_INIT_FULL BIT(7)

/*
 * Number of times we have reset the touchpad because of errors.
 */
static int tp_reset_retry_count;

#define MAX_TP_RESET_RETRY_COUNT 3

static int dump_memory_on_error;

/*
 * Bitmap to track if a finger exists.
 */
static int touch_slot;

/*
 * Timestamp of last interrupt (32 bits are enough as we divide the value by 100
 * and then put it in a 16-bit field).
 */
static uint32_t irq_ts;

/*
 * Cached system info.
 */
static struct st_tp_system_info_t system_info;

static struct {
#if ST_TP_EXTRA_BYTE == 1
	uint8_t extra_byte;
#endif
	union {
		uint8_t bytes[512];
		struct st_tp_host_buffer_header_t buffer_header;
		struct st_tp_host_buffer_heat_map_t heat_map;
		struct st_tp_host_data_header_t data_header;
		struct st_tp_event_t events[32];
		uint32_t dump_info[32];
	} __packed /* anonymous */;
} __packed rx_buf;

#ifdef CONFIG_USB_ISOCHRONOUS
#define USB_ISO_PACKET_SIZE 256
/*
 * Header of each USB pacaket.
 */
struct packet_header_t {
	uint8_t index;

#define HEADER_FLAGS_NEW_FRAME BIT(0)
	uint8_t flags;
} __packed;
BUILD_ASSERT(sizeof(struct packet_header_t) < USB_ISO_PACKET_SIZE);

static struct packet_header_t packet_header;

/* What will be sent to USB interface. */
struct st_tp_usb_packet_t {
#define USB_FRAME_FLAGS_BUTTON BIT(0)
	/*
	 * This will be true if user clicked on touchpad.
	 * TODO(b/70482333): add corresponding code for button signal.
	 */
	uint8_t flags;

	/*
	 * This will be `st_tp_host_buffer_heat_map_t.frame` but each pixel
	 * will be scaled to 8 bits value.
	 */
	uint8_t frame[ST_TOUCH_ROWS * ST_TOUCH_COLS];
} __packed;

/* Next buffer index SPI will write to. */
static volatile uint32_t spi_buffer_index;
/* Next buffer index USB will read from. */
static volatile uint32_t usb_buffer_index;
static struct st_tp_usb_packet_t usb_packet[2]; /* double buffering */
/* How many bytes we have transmitted. */
static size_t transmit_report_offset;

/* Function prototypes */
static int get_heat_map_addr(void);
static void print_frame(void);
static void st_tp_disable_heat_map(void);
static void st_tp_enable_heat_map(void);
static int st_tp_read_frame(void);
static void st_tp_interrupt_send(void);
DECLARE_DEFERRED(st_tp_interrupt_send);
#endif

/* Function implementations */

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
			      struct st_tp_event_t *event, int i)
{
	const int id = event->finger.touch_id;

	/* This is not a finger */
	if (event->finger.touch_type == ST_TP_TOUCH_TYPE_INVALID)
		return i;

	if (event->evt_id == ST_TP_EVENT_ID_ENTER_POINTER)
		touch_slot |= 1 << id;
	else if (event->evt_id == ST_TP_EVENT_ID_LEAVE_POINTER)
		touch_slot &= ~BIT(id);

	/* We cannot report more fingers */
	if (i >= ARRAY_SIZE(report->finger)) {
		CPRINTS("WARN: ST reports more than %d fingers", i);
		return i;
	}

	switch (event->evt_id) {
	case ST_TP_EVENT_ID_ENTER_POINTER:
	case ST_TP_EVENT_ID_MOTION_POINTER:
		/* Pressure == 255 is a palm. */
		report->finger[i].confidence = (event->finger.z < 255);
		report->finger[i].tip = 1;
		report->finger[i].inrange = 1;
		report->finger[i].id = id;
		report->finger[i].pressure = event->finger.z;
		report->finger[i].width =
			(event->finger.minor | (event->minor_high << 4)) << 5;
		report->finger[i].height =
			(event->finger.major | (event->major_high << 4)) << 5;

		report->finger[i].x = (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X -
				       event->finger.x);
		report->finger[i].y = (CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y -
				       event->finger.y);
		break;
	case ST_TP_EVENT_ID_LEAVE_POINTER:
		report->finger[i].id = id;
		/* When a finger is leaving, it's not a palm */
		report->finger[i].confidence = 1;
		break;
	}
	return i + 1;
}

/*
 * Read domeswitch level from touchpad, and save in `system_state`.
 *
 * After calling this function, use
 *	`system_state & SYSTEM_STATE_DOME_SWITCH_LEVEL`
 * to get current value.
 *
 * @return error code on failure.
 */
static int st_tp_check_domeswitch_state(void)
{
	int ret = st_tp_read_host_buffer_header();

	if (ret)
		return ret;

	ret = rx_buf.buffer_header.flags & ST_TP_BUFFER_HEADER_DOMESWITCH_LVL;
	/*
	 * Domeswitch level from device is inverted.
	 * That is, 0 => pressed, 1 => released.
	 */
	set_bits(&system_state, ret ? 0 : SYSTEM_STATE_DOME_SWITCH_LEVEL,
		 SYSTEM_STATE_DOME_SWITCH_LEVEL);
	return 0;
}

static int st_tp_write_hid_report(void)
{
	int ret, i, num_finger, num_events;
	const int old_system_state = system_state;
	int domeswitch_changed;
	struct usb_hid_touchpad_report report;

	ret = st_tp_check_domeswitch_state();
	if (ret)
		return ret;

	domeswitch_changed = ((old_system_state ^ system_state) &
			      SYSTEM_STATE_DOME_SWITCH_LEVEL);

	num_events = st_tp_read_all_events(1);
	if (tp_control)
		return 1;

	memset(&report, 0, sizeof(report));
	report.id = REPORT_ID_TOUCHPAD;
	num_finger = 0;

	for (i = 0; i < num_events; i++) {
		struct st_tp_event_t *e = &rx_buf.events[i];

		switch (e->evt_id) {
		case ST_TP_EVENT_ID_ENTER_POINTER:
		case ST_TP_EVENT_ID_MOTION_POINTER:
		case ST_TP_EVENT_ID_LEAVE_POINTER:
			num_finger = st_tp_parse_finger(&report, e, num_finger);
			break;
		default:
			break;
		}
	}

	if (!num_finger && !domeswitch_changed) /* nothing changed */
		return 0;

	/* Don't report 0 finger click. */
	if (num_finger && (system_state & SYSTEM_STATE_DOME_SWITCH_LEVEL))
		report.button = 1;
	report.count = num_finger;
	report.timestamp = irq_ts / USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

	set_touchpad_report(&report);
	return 0;
}

static int st_tp_read_report(void)
{
	if (system_state & SYSTEM_STATE_ENABLE_HEAT_MAP) {
#ifdef CONFIG_USB_ISOCHRONOUS
		/*
		 * Because we are using double buffering, so, if
		 * usb_buffer_index = N
		 *
		 * 1. spi_buffer_index == N      => ok, both slots are empty
		 * 2. spi_buffer_index == N + 1  => ok, second slot is empty
		 * 3. spi_buffer_index == N + 2  => not ok, need to wait for USB
		 */
		if (spi_buffer_index - usb_buffer_index <= 1) {
			if (st_tp_read_frame() == EC_SUCCESS) {
				spi_buffer_index++;
				if (system_state & SYSTEM_STATE_DEBUG_MODE) {
					print_frame();
					usb_buffer_index++;
				}
			}
		}
		if (spi_buffer_index > usb_buffer_index)
			hook_call_deferred(&st_tp_interrupt_send_data, 0);
#endif
	} else {
		st_tp_write_hid_report();
	}
	return st_tp_send_ack();
}

static int st_tp_read_host_buffer_header(void)
{
	const uint8_t tx_buf[] = { ST_TP_CMD_READ_SPI_HOST_BUFFER, 0x00, 0x00 };
	int rx_len = ST_TP_EXTRA_BYTE + sizeof(rx_buf.buffer_header);

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), (uint8_t *)&rx_buf,
			       rx_len);
}

static int st_tp_send_ack(void)
{
	uint8_t tx_buf[] = { ST_TP_CMD_SPI_HOST_BUFFER_ACK };

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int st_tp_update_system_state(int new_state, int mask)
{
	int ret = EC_SUCCESS;
	int need_locked_scan_mode = 0;

	/* copy reserved bits */
	set_bits(&new_state, system_state, ~mask);

	mask = SYSTEM_STATE_DEBUG_MODE;
	if ((new_state & mask) != (system_state & mask))
		set_bits(&system_state, new_state, mask);

	mask = SYSTEM_STATE_ENABLE_HEAT_MAP | SYSTEM_STATE_ENABLE_DOME_SWITCH;
	if ((new_state & mask) != (system_state & mask)) {
		uint8_t tx_buf[] = { ST_TP_CMD_WRITE_FEATURE_SELECT, 0x05, 0 };
		if (new_state & SYSTEM_STATE_ENABLE_HEAT_MAP) {
			CPRINTS("Heatmap enabled");
			tx_buf[2] |= BIT(0);
			need_locked_scan_mode = 1;
		} else {
			CPRINTS("Heatmap disabled");
		}

		if (new_state & SYSTEM_STATE_ENABLE_DOME_SWITCH)
			tx_buf[2] |= BIT(1);
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

	/*
	 * We need to lock scan mode to prevent scan rate drop when heat map
	 * mode is enabled.
	 */
	if (need_locked_scan_mode) {
		uint8_t tx_buf[] = {
			ST_TP_CMD_WRITE_SCAN_MODE_SELECT,
			ST_TP_SCAN_MODE_LOCKED,
			0x0,
		};

		ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
		if (ret)
			return ret;
	}
	return ret;
}

static void st_tp_enable_interrupt(int enable)
{
	uint8_t tx_buf[] = { ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x01,
			     enable ? 1 : 0 };
	if (enable)
		gpio_enable_interrupt(GPIO_TOUCHPAD_INT);
	spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
	if (!enable)
		gpio_disable_interrupt(GPIO_TOUCHPAD_INT);
}

static int st_tp_start_scan(void)
{
	int new_state =
		(SYSTEM_STATE_ACTIVE_MODE | SYSTEM_STATE_ENABLE_DOME_SWITCH);
	int mask = new_state;
	int ret;

	CPRINTS("ST: Start scanning");
	ret = st_tp_update_system_state(new_state, mask);
	if (ret)
		return ret;
	st_tp_send_ack();
	st_tp_enable_interrupt(1);

	return ret;
}

static int st_tp_read_host_data_memory(uint16_t addr, void *rx_buf, int len)
{
	uint8_t tx_buf[] = { ST_TP_CMD_READ_HOST_DATA_MEMORY, addr >> 8,
			     addr & 0xFF };

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), rx_buf, len);
}

static int st_tp_stop_scan(void)
{
	int new_state = 0;
	int mask = SYSTEM_STATE_ACTIVE_MODE;
	int ret;

	CPRINTS("ST: Stop scanning");
	ret = st_tp_update_system_state(new_state, mask);
	st_tp_enable_interrupt(0);

	return ret;
}

static int st_tp_load_host_data(uint8_t mem_id)
{
	uint8_t tx_buf[] = { ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x06, mem_id };
	int retry, ret;
	uint16_t count;
	struct st_tp_host_data_header_t *header = &rx_buf.data_header;
	int rx_len = sizeof(*header) + ST_TP_EXTRA_BYTE;

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
		crec_msleep(10);
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
	int rx_len = ST_TP_EXTRA_BYTE + ST_TP_SYSTEM_INFO_LEN;
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
#define ST_TP_SHOW64(attr) CPRINTS(#attr ": %04llx", system_info.attr)
	ST_TP_SHOW64(release_info);
#undef ST_TP_SHOW
#undef ST_TP_SHOW64
	return ret;
}

/*
 * Enable / disable deep sleep on memory and bus.
 *
 * Before calling dump_error() and dump_error(), deep sleep should be disabled,
 * otherwise response data might be garbage.
 */
static void enable_deep_sleep(int enable)
{
	uint8_t cmd[] = { 0xFA, 0x20, 0x00, 0x00, 0x68, enable ? 0x0B : 0x08 };

	spi_transaction(SPI, cmd, sizeof(cmd), NULL, 0);
}

static void dump_error(void)
{
	uint8_t tx_buf[] = { 0xFB, 0x20, 0x01, 0xEF, 0x80 };
	int rx_len = sizeof(rx_buf.dump_info) + ST_TP_EXTRA_BYTE;
	int i;

	spi_transaction(SPI, tx_buf, sizeof(tx_buf), (uint8_t *)&rx_buf,
			rx_len);

	for (i = 0; i < ARRAY_SIZE(rx_buf.dump_info); i += 4)
		CPRINTS("%08x %08x %08x %08x", rx_buf.dump_info[i + 0],
			rx_buf.dump_info[i + 1], rx_buf.dump_info[i + 2],
			rx_buf.dump_info[i + 3]);
	crec_msleep(8);
}

/*
 * Dump entire 64K memory on touchpad.
 *
 * This is very time consuming.  For now, let's disable this in production
 * build.
 */
static void dump_memory(void)
{
	uint32_t size = 0x10000, rx_len = 512 + ST_TP_EXTRA_BYTE;
	uint32_t offset, i, j;
	uint8_t cmd[] = { 0xFB, 0x00, 0x10, 0x00, 0x00 };

	if (!dump_memory_on_error)
		return;

	for (offset = 0; offset < size; offset += 512) {
		cmd[3] = (offset >> 8) & 0xFF;
		cmd[4] = (offset >> 0) & 0xFF;
		spi_transaction(SPI, cmd, sizeof(cmd), (uint8_t *)&rx_buf,
				rx_len);

		for (i = 0; i < rx_len - ST_TP_EXTRA_BYTE; i += 32) {
			for (j = 0; j < 8; j++) {
				char str_buf[hex_str_buf_size(4)];

				snprintf_hex_buffer(
					str_buf, sizeof(str_buf),
					HEX_BUF(rx_buf.bytes + i + 4 * j, 4));

				CPRINTF("%s ", str_buf);
			}
			CPRINTF("\n");
			crec_msleep(8);
		}
	}
	CPRINTF("===============================\n");
	crec_msleep(8);
}

/*
 * Set `tp_control` if there are any actions should be taken.
 */
static void st_tp_handle_error(uint8_t error_type)
{
	tp_control |= TP_CONTROL_SHALL_DUMP_ERROR;

	/*
	 * Suggest action: memory dump and power cycle.
	 */
	if (error_type <= 0x06 || error_type == 0xF1 || error_type == 0xF2 ||
	    error_type == 0xF3 || (error_type >= 0x47 && error_type <= 0x4E)) {
		tp_control |= TP_CONTROL_SHALL_RESET;
		return;
	}

	/*
	 * Suggest action: FW shall halt, consult ST.
	 */
	if ((error_type >= 0x20 && error_type <= 0x23) || error_type == 0x25 ||
	    (error_type >= 0x2E && error_type <= 0x46)) {
		CPRINTS("tp shall halt");
		tp_control |= TP_CONTROL_SHALL_HALT;
		return;
	}

	/*
	 * Corrupted panel configuration, a panel init should fix it.
	 */
	if (error_type >= 0x28 && error_type <= 0x29) {
		tp_control |= TP_CONTROL_SHALL_INIT;
		return;
	}

	/*
	 * Corrupted CX section, a full panel init should fix it.
	 */
	if (error_type >= 0xA0 && error_type <= 0xA6) {
		tp_control |= TP_CONTROL_SHALL_INIT_FULL;
		return;
	}

	/*
	 * When 0xFF is received, it's very likely ST touchpad is down.
	 * Try if touchpad can be recovered by reset.
	 */
	if (error_type == 0xFF) {
		if (tp_reset_retry_count < MAX_TP_RESET_RETRY_COUNT) {
			tp_control |= TP_CONTROL_SHALL_RESET;
			tp_reset_retry_count++;
		} else {
			tp_control |= TP_CONTROL_SHALL_HALT;
		}
		return;
	}
}

/*
 * Handles error reports.
 */
static void st_tp_handle_error_report(struct st_tp_event_t *e)
{
	uint8_t error_type = e->report.report_type;

	CPRINTS("Touchpad error: %x %x", error_type,
		((e->report.info[0] << 0) | (e->report.info[1] << 8) |
		 (e->report.info[2] << 16) | (e->report.info[3] << 24)));

	st_tp_handle_error(error_type);
}

static void st_tp_handle_status_report(struct st_tp_event_t *e)
{
	static uint32_t prev_idle_count;
	uint32_t info = ((e->report.info[0] << 0) | (e->report.info[1] << 8) |
			 (e->report.info[2] << 16) | (e->report.info[3] << 24));

	if (e->report.report_type == ST_TP_STATUS_FCAL ||
	    e->report.report_type == ST_TP_STATUS_FRAME_DROP)
		CPRINTS("TP STATUS REPORT: %02x %08x", e->report.report_type,
			info);

	/*
	 * Idle count might not change if ST FW is busy (for example, when the
	 * user puts a big palm on touchpad).  Therefore if idle count doesn't
	 * change, we need to double check with touch count.
	 *
	 * If touch count is 0, and idle count doesn't change, it means that:
	 *
	 *   1) ST doesn't think there are any fingers.
	 *   2) ST is busy on something, can't get into idle mode, and this
	 *      might cause (1).
	 *
	 * Resetting touchpad should be the correct action.
	 */
	if (e->report.report_type == ST_TP_STATUS_BEACON) {
#if 0
		const uint8_t touch_count = e->report.reserved;

		CPRINTS("BEACON: idle count=%08x", info);
		CPRINTS("  touch count=%d  touch slot=%04x",
			touch_count, touch_slot);
#endif
		if (prev_idle_count == info && touch_slot == 0) {
			CPRINTS("  idle count=%08x not changed", info);
			tp_control |= TP_CONTROL_SHALL_RESET;
			return;
		}
		prev_idle_count = info;
	}
}

/*
 * Read all events, and handle errors.
 *
 * When there are error events, suggested action will be saved in `tp_control`.
 *
 * @param show_error: whether EC should read and dump error or not.
 *   ***If this is true, rx_buf.events[] will be cleared.***
 *
 * @return number of events available
 */
static int st_tp_read_all_events(int show_error)
{
	uint8_t cmd = ST_TP_CMD_READ_ALL_EVENTS;
	int rx_len = sizeof(rx_buf.events) + ST_TP_EXTRA_BYTE;
	int i;

	if (spi_transaction(SPI, &cmd, 1, (uint8_t *)&rx_buf, rx_len))
		return 0;

	for (i = 0; i < ARRAY_SIZE(rx_buf.events); i++) {
		struct st_tp_event_t *e = &rx_buf.events[i];

		if (e->magic != ST_TP_EVENT_MAGIC)
			break;

		switch (e->evt_id) {
		case ST_TP_EVENT_ID_ERROR_REPORT:
			st_tp_handle_error_report(e);
			break;
		case ST_TP_EVENT_ID_STATUS_REPORT:
			st_tp_handle_status_report(e);
			break;
		}
	}

	if (show_error && (tp_control & TP_CONTROL_SHALL_DUMP_ERROR)) {
		enable_deep_sleep(0);
		dump_error();
		dump_memory();
		enable_deep_sleep(1);
		/* rx_buf.events[] is invalid now */
		i = 0;
	}
	tp_control &= ~TP_CONTROL_SHALL_DUMP_ERROR;

	return i;
}

/*
 * Reset touchpad.  This function will wait for "controller ready" event after
 * the touchpad is reset.
 */
static int st_tp_reset(void)
{
	int i, num_events, retry = 100;

	board_touchpad_reset();

	while (retry--) {
		num_events = st_tp_read_all_events(0);

		/*
		 * We are not doing full panel initialization, and error code
		 * suggest us to reset or halt.
		 */
		if (!(tp_control & (TP_CONTROL_INIT | TP_CONTROL_INIT_FULL)) &&
		    (tp_control &
		     (TP_CONTROL_SHALL_HALT | TP_CONTROL_SHALL_RESET)))
			break;

		for (i = 0; i < num_events; i++) {
			struct st_tp_event_t *e = &rx_buf.events[i];

			if (e->evt_id == ST_TP_EVENT_ID_CONTROLLER_READY) {
				CPRINTS("Touchpad ready");
				tp_reset_retry_count = 0;
				return 0;
			}
		}

		crec_msleep(10);
	}
	CPRINTS("Timeout waiting for controller ready.");
	return EC_ERROR_TIMEOUT;
}

/* Initialize the controller ICs after reset */
static void st_tp_init(void)
{
	tp_control = 0;
	system_state = 0;

	if (st_tp_reset())
		return;

	if (tp_control) {
		CPRINTS("tp_control = %x", tp_control);
		return;
	}
	/*
	 * On boot, ST firmware will load system info to host data memory,
	 * So we don't need to reload it.
	 */
	st_tp_read_system_info(0);

	system_state = SYSTEM_STATE_READY;
	touch_slot = 0;

	touchpad_power_control();
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
		ST_TP_CMD_WRITE_HW_REG, (address >> 24) & 0xFF,
		(address >> 16) & 0xFF, (address >> 8) & 0xFF,
		(address >> 0) & 0xFF,	(data >> 24) & 0xFF,
		(data >> 16) & 0xFF,	(data >> 8) & 0xFF,
		(data >> 0) & 0xFF,
	};

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int write_hwreg_cmd8(uint32_t address, uint8_t data)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_WRITE_HW_REG, (address >> 24) & 0xFF,
		(address >> 16) & 0xFF, (address >> 8) & 0xFF,
		(address >> 0) & 0xFF,	data,
	};

	return spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);
}

static int wait_for_flash_ready(uint8_t type)
{
	uint8_t tx_buf[] = {
		ST_TP_CMD_READ_HW_REG, 0x20, 0x00, 0x00, type,
	};
	int ret = EC_SUCCESS, retry = 200;

	while (retry--) {
		ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf),
				      (uint8_t *)&rx_buf, 1 + ST_TP_EXTRA_BYTE);
		if (ret == EC_SUCCESS && !(rx_buf.bytes[0] & 0x80))
			break;
		crec_msleep(50);
	}
	return retry >= 0 ? ret : EC_ERROR_TIMEOUT;
}

static int erase_flash(int full_init_required)
{
	int ret;

	if (full_init_required)
		ret = write_hwreg_cmd32(0x20000128, 0xFFFFFFFF);
	else
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

static int st_tp_prepare_for_update(int full_init_required)
{
	/* hold m3 */
	write_hwreg_cmd8(0x20000024, 0x01);
	/* unlock flash */
	write_hwreg_cmd8(0x20000025, 0x20);
	/* unlock flash erase */
	write_hwreg_cmd8(0x200000DE, 0x03);
	erase_flash(full_init_required);

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

static int st_tp_write_one_chunk(const uint8_t *head, uint32_t addr,
				 uint32_t chunk_size)
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
	uint8_t tx_buf[12] = { 0 };
	const uint8_t *head = data, *tail = data + size;
	uint32_t addr, index, chunk_size;
	uint32_t flash_buffer_size;
	int ret;

	offset >>= 2; /* offset should be count in words */
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
		tx_buf[index++] = 0x72; /* flash DMA config */
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

static int st_tp_check_command_echo(const uint8_t *cmd, const size_t len)
{
	int num_events, i;
	num_events = st_tp_read_all_events(0);

	for (i = 0; i < num_events; i++) {
		struct st_tp_event_t *e = &rx_buf.events[i];

		if (e->evt_id == ST_TP_EVENT_ID_STATUS_REPORT &&
		    e->report.report_type == ST_TP_STATUS_CMD_ECHO &&
		    memcmp(e->report.info, cmd, MIN(4, len)) == 0)
			return EC_SUCCESS;
	}
	return EC_ERROR_BUSY;
}

static uint8_t get_cx_version(uint8_t tp_version)
{
	/*
	 * CX version is tracked by ST release note: go/whiskers-st-release-note
	 */

	if (tp_version >= 32)
		return 3;

	if (tp_version >= 20)
		return 2;

	if (tp_version >= 18)
		return 1;
	return 0;
}

/*
 * Perform panel initialization.
 *
 * This function will wait until the initialization is done, or 10 second
 * timeout is reached.
 *
 * @param full: 1 => force "full" panel initialization.  Otherwise, tp_control
 *              will be checked to decide if full panel initialization is
 *              required.
 *
 * @return EC_SUCCESS or error code.
 */
static int st_tp_panel_init(int full)
{
	uint8_t tx_buf[] = { ST_TP_CMD_WRITE_SYSTEM_COMMAND, 0x00, 0x02 };
	int ret, retry;

	if (tp_control & (TP_CONTROL_INIT | TP_CONTROL_INIT_FULL))
		return EC_ERROR_BUSY;

	st_tp_stop_scan();
	ret = st_tp_reset();
	/*
	 * TODO(b:118312397): Figure out how to handle st_tp_reset errors (if
	 * needed at all).
	 */
	CPRINTS("st_tp_reset ret=%d", ret);

	full |= tp_control & TP_CONTROL_SHALL_INIT_FULL;
	if (full) {
		/* should perform full panel initialization */
		tx_buf[2] = 0x3;
		tp_control = TP_CONTROL_INIT_FULL;
	} else {
		tp_control = TP_CONTROL_INIT;
	}

	CPRINTS("Start panel initialization (full=%d)", full);
	spi_transaction(SPI, tx_buf, sizeof(tx_buf), NULL, 0);

	retry = 100;
	while (retry--) {
		watchdog_reload();
		crec_msleep(100);

		ret = st_tp_check_command_echo(tx_buf, sizeof(tx_buf));
		if (ret == EC_SUCCESS) {
			CPRINTS("Panel initialization completed.");
			tp_control &= ~(TP_CONTROL_INIT | TP_CONTROL_INIT_FULL);
			st_tp_init();
			return EC_SUCCESS;
		} else if (ret == EC_ERROR_BUSY) {
			CPRINTS("Panel initialization on going...");
		} else if (tp_control &
			   ~(TP_CONTROL_INIT | TP_CONTROL_INIT_FULL)) {
			/* there are other kind of errors. */
			CPRINTS("Panel initialization failed, tp_control: %x",
				tp_control);
			return EC_ERROR_UNKNOWN;
		}
	}
	return EC_ERROR_TIMEOUT;
}

/*
 * @param offset: should be address between 0 to 1M, aligned with
 *	ST_TP_DMA_CHUNK_SIZE.
 * @param size: length of `data` array.
 * @param data: content of new touchpad firmware.
 */
int touchpad_update_write(int offset, int size, const uint8_t *data)
{
	static int full_init_required;
	int ret, flash_offset;

	CPRINTS("%s %08x %d", __func__, offset, size);
	if (offset == 0) {
		const struct st_tp_fw_header_t *header;
		uint8_t old_cx_version;
		uint8_t new_cx_version;
		int retry;

		header = (const struct st_tp_fw_header_t *)data;
		if (header->signature != 0xAA55AA55)
			return EC_ERROR_INVAL;

		for (retry = 50; retry > 0; retry--) {
			watchdog_reload();
			if (system_state & SYSTEM_STATE_READY)
				break;
			if (retry % 10 == 0)
				CPRINTS("TP not ready for update, "
					"will check again");
			crec_msleep(100);
		}

		old_cx_version = get_cx_version(system_info.release_info);
		new_cx_version = get_cx_version(header->release_info);

		full_init_required = old_cx_version != new_cx_version;

		/* stop scanning, interrupt, etc... */
		st_tp_stop_scan();

		ret = st_tp_prepare_for_update(full_init_required);
		if (ret)
			return ret;
		return EC_SUCCESS;
	}

	flash_offset = offset - CONFIG_UPDATE_PDU_SIZE;
	if (flash_offset % ST_TP_DMA_CHUNK_SIZE)
		return EC_ERROR_INVAL;

	if (flash_offset >= ST_TP_FLASH_OFFSET_PANEL_CFG &&
	    flash_offset < ST_TP_FLASH_OFFSET_CONFIG)
		/* don't update CX section && panel config section */
		return EC_SUCCESS;

	ret = st_tp_write_flash(flash_offset, size, data);
	if (ret)
		return ret;

	if (offset + size == CONFIG_TOUCHPAD_VIRTUAL_SIZE) {
		CPRINTS("%s: End update, wait for reset.", __func__);

		ret = st_tp_panel_init(full_init_required);
		task_set_event(TASK_ID_TOUCHPAD, TASK_EVENT_TP_UPDATED);
		return ret;
	}

	return EC_SUCCESS;
}

int touchpad_debug(const uint8_t *param, unsigned int param_size,
		   uint8_t **data, unsigned int *data_size)
{
	static uint8_t buf[8];
	int num_events;

	if (param_size != 1)
		return EC_RES_INVALID_PARAM;

	switch (*param) {
	case ST_TP_DEBUG_CMD_RESET_TOUCHPAD:
		*data = NULL;
		*data_size = 0;
		st_tp_stop_scan();
		hook_call_deferred(&st_tp_init_data, 100 * MSEC);
		return EC_SUCCESS;
	case ST_TP_DEBUG_CMD_CALIBRATE:
		/* no return value */
		*data = NULL;
		*data_size = 0;
		st_tp_panel_init(1);
		return EC_SUCCESS;
	case ST_TP_DEBUG_CMD_START_SCAN:
		*data = NULL;
		*data_size = 0;
		st_tp_start_scan();
		return EC_SUCCESS;
	case ST_TP_DEBUG_CMD_STOP_SCAN:
		*data = NULL;
		*data_size = 0;
		st_tp_stop_scan();
		return EC_SUCCESS;
	case ST_TP_DEBUG_CMD_READ_BUF_HEADER: {
		char str_buf[hex_str_buf_size(8)];
		*data = buf;
		*data_size = 8;
		st_tp_read_host_buffer_header();
		memcpy(buf, rx_buf.bytes, *data_size);
		snprintf_hex_buffer(str_buf, sizeof(str_buf),
				    HEX_BUF(buf, *data_size));
		CPRINTS("header: %s", str_buf);
		return EC_SUCCESS;
	}
	case ST_TP_DEBUG_CMD_READ_EVENTS:
		num_events = st_tp_read_all_events(0);
		if (num_events) {
			int i;

			for (i = 0; i < num_events; i++) {
				CPRINTS("event[%d]: id=%d, type=%d", i,
					rx_buf.events[i].evt_id,
					rx_buf.events[i].report.report_type);
			}
		}
		*data = buf;
		*data_size = 1;
		*data[0] = num_events;
		st_tp_send_ack();
		return EC_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}
#endif

void touchpad_interrupt(enum gpio_signal signal)
{
	irq_ts = __hw_clock_source_read();

	task_wake(TASK_ID_TOUCHPAD);
}

static int touchpad_should_enable(void)
{
	/* touchpad is not ready. */
	if (tp_control)
		return 0;

#ifdef CONFIG_USB_SUSPEND
	if (usb_is_suspended() && !usb_is_remote_wakeup_enabled())
		return 0;
#endif

#ifdef CONFIG_TABLET_MODE
	if (tablet_get_mode())
		return 0;
#endif
	return 1;
}

/* Make a decision on touchpad power, based on USB and tablet mode status. */
static void touchpad_power_control(void)
{
	const int enabled = !!(system_state & SYSTEM_STATE_ACTIVE_MODE);
	int enable = touchpad_should_enable();

	if (enabled == enable)
		return;

	if (enable)
		st_tp_start_scan();
	else
		st_tp_stop_scan();
}

static void touchpad_read_idle_count(void)
{
	static uint32_t prev_count;
	uint32_t count;
	int ret;
	int rx_len = 2 + ST_TP_EXTRA_BYTE;
	uint8_t cmd_read_counter[] = { 0xFB, 0x00, 0x10, 0xff, 0xff };

	/* Find address of idle count. */
	ret = st_tp_load_host_data(ST_TP_MEM_ID_SYSTEM_INFO);
	if (ret)
		return;
	st_tp_read_host_data_memory(0x0082, &rx_buf, rx_len);

	/* Fill in address of idle count, the byte order is reversed. */
	cmd_read_counter[3] = rx_buf.bytes[1];
	cmd_read_counter[4] = rx_buf.bytes[0];

	/* Read idle count */
	spi_transaction(SPI, cmd_read_counter, sizeof(cmd_read_counter),
			(uint8_t *)&rx_buf, 4 + ST_TP_EXTRA_BYTE);

	count = rx_buf.dump_info[0];

	CPRINTS("idle_count = %08x", count);
	if (count == prev_count)
		CPRINTS("counter doesn't change...");
	else
		prev_count = count;
}

/*
 * Try to collect symptoms of type B error.
 *
 * There are three possible symptoms:
 *   1. error dump section is corrupted / contains error.
 *   2. memory stack is corrupted (not 0xCC).
 *   3. idle count is not changing.
 */
static void touchpad_collect_error(void)
{
	const uint8_t tx_dump_error[] = { 0xFB, 0x20, 0x01, 0xEF, 0x80 };
	uint32_t dump_info[2];
	const uint8_t tx_dump_memory[] = { 0xFB, 0x00, 0x10, 0x00, 0x00 };
	uint32_t dump_memory[16];
	int i;

	enable_deep_sleep(0);
	spi_transaction(SPI, tx_dump_error, sizeof(tx_dump_error),
			(uint8_t *)&rx_buf,
			sizeof(dump_info) + ST_TP_EXTRA_BYTE);
	memcpy(dump_info, rx_buf.bytes, sizeof(dump_info));

	spi_transaction(SPI, tx_dump_memory, sizeof(tx_dump_memory),
			(uint8_t *)&rx_buf,
			sizeof(dump_memory) + ST_TP_EXTRA_BYTE);
	memcpy(dump_memory, rx_buf.bytes, sizeof(dump_memory));

	CPRINTS("check error dump: %08x %08x", dump_info[0], dump_info[1]);
	CPRINTS("check memory dump:");
	for (i = 0; i < ARRAY_SIZE(dump_memory); i += 8) {
		CPRINTF("%08x %08x %08x %08x %08x %08x %08x %08x\n",
			dump_memory[i + 0], dump_memory[i + 1],
			dump_memory[i + 2], dump_memory[i + 3],
			dump_memory[i + 4], dump_memory[i + 5],
			dump_memory[i + 6], dump_memory[i + 7]);
	}

	for (i = 0; i < 3; i++)
		touchpad_read_idle_count();
	enable_deep_sleep(1);

	tp_control |= TP_CONTROL_SHALL_RESET;
}

void touchpad_task(void *u)
{
	uint32_t event;

	while (1) {
		uint32_t retry;

		for (retry = 0; retry < 3; retry++) {
			CPRINTS("st_tp_init: trial %d", retry + 1);
			st_tp_init();

			if (system_state & SYSTEM_STATE_READY)
				break;
			/*
			 * React on touchpad errors.
			 */
			if (tp_control & TP_CONTROL_SHALL_INIT_FULL) {
				/* suppress other handlers */
				tp_control = TP_CONTROL_SHALL_INIT_FULL;
				st_tp_panel_init(1);
			} else if (tp_control & TP_CONTROL_SHALL_INIT) {
				/* suppress other handlers */
				tp_control = TP_CONTROL_SHALL_INIT;
				st_tp_panel_init(0);
			} else if (tp_control & TP_CONTROL_SHALL_RESET) {
				/* suppress other handlers */
				tp_control = TP_CONTROL_SHALL_RESET;
			} else if (tp_control & TP_CONTROL_SHALL_HALT) {
				CPRINTS("shall halt");
				tp_control = 0;
				break;
			}
		}

		if (system_state & SYSTEM_STATE_READY)
			break;

		/* failed to init, mark it as ready to allow upgrade */
		system_state = SYSTEM_STATE_READY;
		/* wait for upgrade complete */
		task_wait_event_mask(TASK_EVENT_TP_UPDATED, -1);
	}
	touchpad_power_control();

	while (1) {
		/* wait for at most 3 seconds */
		event = task_wait_event(3 * 1000 * 1000);

		if ((event & TASK_EVENT_TIMER) &&
		    (system_state & SYSTEM_STATE_ACTIVE_MODE))
			/*
			 * Haven't received anything for 3 seconds, and we are
			 * supposed to be in active mode.  This is not normal,
			 * check for errors and reset.
			 */
			touchpad_collect_error();

		if (event & TASK_EVENT_WAKE)
			while (!tp_control &&
			       !gpio_get_level(GPIO_TOUCHPAD_INT))
				st_tp_read_report();

		/*
		 * React on touchpad errors.
		 */
		if (tp_control & TP_CONTROL_SHALL_INIT_FULL) {
			/* suppress other handlers */
			tp_control = TP_CONTROL_SHALL_INIT_FULL;
			st_tp_panel_init(1);
		} else if (tp_control & TP_CONTROL_SHALL_INIT) {
			/* suppress other handlers */
			tp_control = TP_CONTROL_SHALL_INIT;
			st_tp_panel_init(0);
		} else if (tp_control & TP_CONTROL_SHALL_RESET) {
			/* suppress other handlers */
			tp_control = TP_CONTROL_SHALL_RESET;
			st_tp_init();
		} else if (tp_control & TP_CONTROL_SHALL_HALT) {
			tp_control = 0;
			st_tp_stop_scan();
		}

		if (event & TASK_EVENT_POWER)
			touchpad_power_control();
	}
}

/*
 * When USB PM status changes, or tablet mode changes, call in the main task to
 * decide whether to turn touchpad on or off.
 */
#if defined(CONFIG_USB_SUSPEND) || defined(CONFIG_TABLET_MODE)
static void touchpad_power_change(void)
{
	task_set_event(TASK_ID_TOUCHPAD, TASK_EVENT_POWER);
}
#endif
#ifdef CONFIG_USB_SUSPEND
DECLARE_HOOK(HOOK_USB_PM_CHANGE, touchpad_power_change, HOOK_PRIO_DEFAULT);
#endif
#ifdef CONFIG_TABLET_MODE
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, touchpad_power_change, HOOK_PRIO_DEFAULT);
#endif

#ifdef CONFIG_USB_ISOCHRONOUS
static void st_tp_enable_heat_map(void)
{
	int new_state =
		(SYSTEM_STATE_ENABLE_HEAT_MAP |
		 SYSTEM_STATE_ENABLE_DOME_SWITCH | SYSTEM_STATE_ACTIVE_MODE);
	int mask = new_state;

	st_tp_update_system_state(new_state, mask);
}
DECLARE_DEFERRED(st_tp_enable_heat_map);

static void st_tp_disable_heat_map(void)
{
	int new_state = 0;
	int mask = SYSTEM_STATE_ENABLE_HEAT_MAP;

	st_tp_update_system_state(new_state, mask);
}
DECLARE_DEFERRED(st_tp_disable_heat_map);

static void print_frame(void)
{
	char debug_line[ST_TOUCH_COLS + 5];
	int i, j, index;
	int v;
	struct st_tp_usb_packet_t *packet = &usb_packet[usb_buffer_index & 1];

	if (usb_buffer_index == spi_buffer_index)
		/* buffer is empty. */
		return;

	/* We will have ~150 FPS, let's print ~4 frames per second */
	if (usb_buffer_index % 37 == 0) {
		/* move cursor back to top left corner */
		CPRINTF("\x1b[H");
		CPUTS("==============\n");
		for (i = 0; i < ST_TOUCH_ROWS; i++) {
			for (j = 0; j < ST_TOUCH_COLS; j++) {
				index = i * ST_TOUCH_COLS;
				index += (ST_TOUCH_COLS - j - 1); // flip X
				v = packet->frame[index];

				if (v > 0)
					debug_line[j] = '0' + v * 10 / 256;
				else
					debug_line[j] = ' ';
			}
			debug_line[j++] = '\n';
			debug_line[j++] = '\0';
			CPRINTF(debug_line);
		}
		CPUTS("==============\n");
	}
}

static int st_tp_read_frame(void)
{
	int ret = EC_SUCCESS;
	int rx_len = ST_TOUCH_FRAME_SIZE + ST_TP_EXTRA_BYTE;
	int heat_map_addr = get_heat_map_addr();
	uint8_t tx_buf[] = {
		ST_TP_CMD_READ_SPI_HOST_BUFFER,
		(heat_map_addr >> 8) & 0xFF,
		(heat_map_addr >> 0) & 0xFF,
	};

	/*
	 * Since usb_packet.frame is already ane uint8_t byte array, we can just
	 * make it the RX buffer for SPI transaction.
	 *
	 * When there is a extra byte, since we know that flags is a one byte
	 * value, and we will override it later, it's okay for SPI transaction
	 * to write the extra byte to flags address.
	 */
#if ST_TP_EXTRA_BYTE == 1
	BUILD_ASSERT(sizeof(usb_packet[0].flags) == 1);
	uint8_t *rx_buf = &usb_packet[spi_buffer_index & 1].flags;
#else
	uint8_t *rx_buf = usb_packet[spi_buffer_index & 1].frame;
#endif

	st_tp_read_all_events(1);
	if (tp_control) {
		ret = EC_ERROR_UNKNOWN;
		goto failed;
	}

	if (heat_map_addr < 0)
		goto failed;

	ret = st_tp_check_domeswitch_state();
	if (ret)
		goto failed;

	/*
	 * Theoretically, we should read host buffer header to check if data is
	 * valid, but the data should always be ready when interrupt pin is low.
	 * Let's skip this check for now.
	 */
	ret = spi_transaction(SPI, tx_buf, sizeof(tx_buf), (uint8_t *)rx_buf,
			      rx_len);
	if (ret == EC_SUCCESS) {
		int i;
		uint8_t *dest = usb_packet[spi_buffer_index & 1].frame;
		uint8_t max_value = 0;

		for (i = 0; i < ST_TOUCH_COLS * ST_TOUCH_ROWS; i++)
			max_value |= dest[i];
		if (max_value == 0) // empty frame
			return -1;

		usb_packet[spi_buffer_index & 1].flags = 0;
		if (system_state & SYSTEM_STATE_DOME_SWITCH_LEVEL)
			usb_packet[spi_buffer_index & 1].flags |=
				USB_FRAME_FLAGS_BUTTON;
	}
failed:
	return ret;
}

/* Define USB interface for heat_map */

/* function prototypes */
static int st_tp_usb_set_interface(usb_uint alternate_setting,
				   usb_uint interface);
static int heatmap_send_packet(struct usb_isochronous_config const *config);
static void st_tp_usb_tx_callback(struct usb_isochronous_config const *config);

/* USB descriptors */
USB_ISOCHRONOUS_CONFIG_FULL(usb_st_tp_heatmap_config, USB_IFACE_ST_TOUCHPAD,
			    USB_CLASS_VENDOR_SPEC, USB_SUBCLASS_GOOGLE_HEATMAP,
			    USB_PROTOCOL_GOOGLE_HEATMAP,
			    USB_STR_HEATMAP_NAME, /* interface name */
			    USB_EP_ST_TOUCHPAD, USB_ISO_PACKET_SIZE,
			    st_tp_usb_tx_callback, st_tp_usb_set_interface,
			    1 /* 1 extra EP for interrupts */)

/* ***This function will be executed in interrupt context*** */
void st_tp_usb_tx_callback(struct usb_isochronous_config const *config)
{
	task_wake(TASK_ID_HEATMAP);
}

void heatmap_task(void *unused)
{
	struct usb_isochronous_config const *config;

	config = &usb_st_tp_heatmap_config;

	while (1) {
		/* waiting st_tp_usb_tx_callback() */
		task_wait_event(-1);

		if (system_state & SYSTEM_STATE_DEBUG_MODE)
			continue;

		if (usb_buffer_index == spi_buffer_index)
			/* buffer is empty */
			continue;

		while (heatmap_send_packet(config))
			/* We failed to write a packet, try again later. */
			task_wait_event(100);
	}
}

/* USB interface has completed TX, it's asking for more data */
static int heatmap_send_packet(struct usb_isochronous_config const *config)
{
	size_t num_byte_available;
	size_t offset = 0;
	int ret, buffer_id = -1;
	struct st_tp_usb_packet_t *packet = &usb_packet[usb_buffer_index & 1];

	packet_header.flags = 0;
	num_byte_available = sizeof(*packet) - transmit_report_offset;
	if (num_byte_available > 0) {
		if (transmit_report_offset == 0)
			packet_header.flags |= HEADER_FLAGS_NEW_FRAME;
		ret = usb_isochronous_write_buffer(config,
						   (uint8_t *)&packet_header,
						   sizeof(packet_header),
						   offset, &buffer_id, 0);
		/*
		 * Since USB_ISO_PACKET_SIZE > sizeof(packet_header), this must
		 * be true.
		 */
		if (ret != sizeof(packet_header))
			return -1;

		offset += ret;
		packet_header.index++;

		ret = usb_isochronous_write_buffer(
			config, (uint8_t *)packet + transmit_report_offset,
			num_byte_available, offset, &buffer_id, 1);
		if (ret < 0) {
			/*
			 * TODO(b/70482333): handle this error, it might be:
			 *   1. timeout (buffer_id changed)
			 *   2. invalid offset
			 *
			 * For now, let's just return an error and try again.
			 */
			CPRINTS("%s %d: %d", __func__, __LINE__, -ret);
			return ret;
		}

		/* We should have sent some bytes, update offset */
		transmit_report_offset += ret;
		if (transmit_report_offset == sizeof(*packet)) {
			transmit_report_offset = 0;
			usb_buffer_index++;
		}
	}
	return 0;
}

static int st_tp_usb_set_interface(usb_uint alternate_setting,
				   usb_uint interface)
{
	if (alternate_setting == 1) {
		if ((system_info.release_info & 0xFF) <
		    ST_TP_MIN_HEATMAP_VERSION) {
			CPRINTS("release version %04llx doesn't support heatmap",
				system_info.release_info);
			/* Heatmap mode is not supported in this version. */
			return -1;
		}

		hook_call_deferred(&st_tp_enable_heat_map_data, 0);
		return 0;
	} else if (alternate_setting == 0) {
		hook_call_deferred(&st_tp_disable_heat_map_data, 0);
		return 0;
	} else /* we only have two settings. */
		return -1;
}

static int get_heat_map_addr(void)
{
	/*
	 * TODO(stimim): drop this when we are sure all trackpads are having the
	 * same config (e.g. after EVT).
	 */
	if (system_info.release_info >= 0x3)
		return 0x0120;
	else if (system_info.release_info == 0x1)
		return 0x20;
	else
		return -1; /* Unknown version */
}

struct st_tp_interrupt_t {
#define ST_TP_INT_FRAME_AVAILABLE BIT(0)
	uint8_t flags;
} __packed;

static usb_uint st_tp_usb_int_buffer[DIV_ROUND_UP(
	sizeof(struct st_tp_interrupt_t), 2)] __usb_ram;

const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_ST_TOUCHPAD, 81) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_ST_TOUCHPAD_INT,
	.bmAttributes = 0x03 /* Interrupt endpoint */,
	.wMaxPacketSize = sizeof(struct st_tp_interrupt_t),
	.bInterval = 1 /* ms */,
};

static void st_tp_interrupt_send(void)
{
	struct st_tp_interrupt_t report;

	memset(&report, 0, sizeof(report));

	if (usb_buffer_index < spi_buffer_index)
		report.flags |= ST_TP_INT_FRAME_AVAILABLE;
	memcpy_to_usbram((void *)usb_sram_addr(st_tp_usb_int_buffer), &report,
			 sizeof(report));
	/* enable TX */
	STM32_TOGGLE_EP(USB_EP_ST_TOUCHPAD_INT, EP_TX_MASK, EP_TX_VALID, 0);
	usb_wake();
}

static void st_tp_interrupt_tx(void)
{
	STM32_USB_EP(USB_EP_ST_TOUCHPAD_INT) &= EP_MASK;

	if (usb_buffer_index < spi_buffer_index)
		/* pending frames */
		hook_call_deferred(&st_tp_interrupt_send_data, 0);
}

static void st_tp_interrupt_event(enum usb_ep_event evt)
{
	int ep = USB_EP_ST_TOUCHPAD_INT;

	if (evt == USB_EVENT_RESET) {
		btable_ep[ep].tx_addr = usb_sram_addr(st_tp_usb_int_buffer);
		btable_ep[ep].tx_count = sizeof(struct st_tp_interrupt_t);

		STM32_USB_EP(ep) = ((ep << 0) | EP_TX_VALID |
				    (3 << 9) /* interrupt EP */ | EP_RX_DISAB);
	}
}

USB_DECLARE_EP(USB_EP_ST_TOUCHPAD_INT, st_tp_interrupt_tx, st_tp_interrupt_tx,
	       st_tp_interrupt_event);

#endif

/* Debugging commands */
static int command_touchpad_st(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (strcasecmp(argv[1], "version") == 0) {
		st_tp_read_system_info(1);
		return EC_SUCCESS;
	} else if (strcasecmp(argv[1], "calibrate") == 0) {
		st_tp_panel_init(1);
		return EC_SUCCESS;
	} else if (strcasecmp(argv[1], "enable") == 0) {
#ifdef CONFIG_USB_ISOCHRONOUS
		set_bits(&system_state, SYSTEM_STATE_DEBUG_MODE,
			 SYSTEM_STATE_DEBUG_MODE);
		hook_call_deferred(&st_tp_enable_heat_map_data, 0);
		return 0;
#else
		return EC_ERROR_NOT_HANDLED;
#endif
	} else if (strcasecmp(argv[1], "disable") == 0) {
#ifdef CONFIG_USB_ISOCHRONOUS
		set_bits(&system_state, 0, SYSTEM_STATE_DEBUG_MODE);
		hook_call_deferred(&st_tp_disable_heat_map_data, 0);
		return 0;
#else
		return EC_ERROR_NOT_HANDLED;
#endif
	} else if (strcasecmp(argv[1], "dump") == 0) {
		enable_deep_sleep(0);
		dump_error();
		dump_memory();
		enable_deep_sleep(1);
		return EC_SUCCESS;
	} else if (strcasecmp(argv[1], "memory_dump") == 0) {
		if (argc == 3 && !parse_bool(argv[2], &dump_memory_on_error))
			return EC_ERROR_PARAM2;

		ccprintf("memory_dump: %d\n", dump_memory_on_error);
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM1;
	}
}
DECLARE_CONSOLE_COMMAND(touchpad_st, command_touchpad_st,
			"<enable | disable | version | calibrate | dump | "
			"memory_dump <enable|disable>>",
			"Read write spi. id is spi_devices array index");
