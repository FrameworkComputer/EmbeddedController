/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_ZEPHYR
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#else
#include "byteorder.h"
#endif

#include "common.h"
#include "console.h"
#include "driver/touchpad_elan.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "sha256.h"
#include "shared_mem.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "touchpad.h"
#include "update_fw.h"
#include "usb_api.h"
#include "usb_hid_touchpad.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ##args)

#define TASK_EVENT_POWER TASK_EVENT_CUSTOM_BIT(0)

#if defined(CONFIG_USB_UPDATE)
/* The actual FW_SIZE depends on IC. */
#define FW_SIZE CONFIG_TOUCHPAD_VIRTUAL_SIZE
#elif defined(CONFIG_EMUL_TOUCHPAD_ELAN)
#define FW_SIZE 65536
#endif

#ifdef CONFIG_ZEPHYR
#if DT_HAS_COMPAT_STATUS_OKAY(elan_ekth3000)

#define TP_NODE DT_INST(0, elan_ekth3000)
#define GPIO_TOUCHPAD_INT GPIO_SIGNAL(DT_PROP(DT_PROP(TP_NODE, irq), irq_pin))
#define CONFIG_TOUCHPAD_I2C_ADDR_FLAGS DT_REG_ADDR(TP_NODE)
#define CONFIG_TOUCHPAD_I2C_PORT I2C_PORT_BY_DEV(TP_NODE)
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X DT_PROP(TP_NODE, logical_max_x)
#define CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y DT_PROP(TP_NODE, logical_max_y)
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X DT_PROP(TP_NODE, physical_max_x)
#define CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y DT_PROP(TP_NODE, physical_max_y)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(elan_ekth3000) */
#endif /* CONFIG_ZEPHYR */

struct {
	/* Max X/Y position */
	uint16_t max_x;
	uint16_t max_y;
	/* Scaling factor for finger width/height */
	uint16_t width_x;
	uint16_t width_y;
	/* Pressure adjustment */
	uint8_t pressure_adj;
	/* Device info */
	uint16_t ic_type;
	uint16_t page_count;
	uint16_t page_size;
	uint16_t iap_version;
	/* Version related info */
	uint8_t pattern;
} elan_tp_params;

/*
 * Report a more reasonable pressure value, so that no adjustment is necessary
 * on Chrome OS side. 3216/1024 ~= 3.1416.
 */
const int pressure_mult = 3216;
const int pressure_div = 1024;

static int elan_tp_read_cmd(uint16_t reg, uint16_t *val)
{
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = reg >> 8;

	return i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT,
			CONFIG_TOUCHPAD_I2C_ADDR_FLAGS, buf, sizeof(buf),
			(uint8_t *)val, sizeof(*val));
}

static int elan_tp_write_cmd(uint16_t reg, uint16_t val)
{
	uint8_t buf[4];

	buf[0] = reg;
	buf[1] = reg >> 8;
	buf[2] = val;
	buf[3] = val >> 8;

	return i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT,
			CONFIG_TOUCHPAD_I2C_ADDR_FLAGS, buf, sizeof(buf), NULL,
			0);
}

/* Power is on by default. */
static int elan_tp_power = 1;

static int elan_tp_set_power(int enable)
{
	int rv;
	uint16_t val;

	if ((enable && elan_tp_power) || (!enable && !elan_tp_power))
		return EC_SUCCESS;

	CPRINTS("elan TP power %s", enable ? "on" : "off");

	rv = elan_tp_read_cmd(ETP_I2C_POWER_CMD, &val);
	if (rv)
		goto out;

	if (enable)
		val &= ~ETP_DISABLE_POWER;
	else
		val |= ETP_DISABLE_POWER;

	rv = elan_tp_write_cmd(ETP_I2C_POWER_CMD, val);

	elan_tp_power = enable;
out:
	return rv;
}

static int finger_status[ETP_MAX_FINGERS] = { 0 };

/*
 * Timestamp of last interrupt (32 bits are enough as we divide the value by 100
 * and then put it in a 16-bit field).
 */
static uint32_t irq_ts;

/*
 * Read touchpad report.
 * Returns 0 on success, positive (EC_RES_*) value on I2C error, and a negative
 * value if the I2C transaction is successful but the data is invalid (fairly
 * common).
 */
static int elan_tp_read_report(void)
{
	int rv;
	uint8_t tp_buf[ETP_I2C_REPORT_LEN];
	int i, ri;
	uint8_t touch_info;
	uint8_t hover_info;
	uint8_t *finger = tp_buf + ETP_FINGER_DATA_OFFSET;
	struct usb_hid_touchpad_report report;
	uint16_t timestamp;

	/* Compute and save timestamp early in case another interrupt comes. */
	timestamp = irq_ts / USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR_FLAGS,
		      NULL, 0, tp_buf, ETP_I2C_REPORT_LEN);

	if (rv) {
		CPRINTS("read report error (%d)", rv);
		return rv;
	}

	if (tp_buf[ETP_REPORT_ID_OFFSET] != ETP_REPORT_ID) {
		CPRINTS("Invalid report id (%x)", tp_buf[ETP_REPORT_ID_OFFSET]);
		return -1;
	}

	memset(&report, 0, sizeof(report));
	report.id = 0x01;
	ri = 0; /* Next finger index in HID report */

	touch_info = tp_buf[ETP_TOUCH_INFO_OFFSET];
	hover_info = tp_buf[ETP_HOVER_INFO_OFFSET];

	for (i = 0; i < ETP_MAX_FINGERS; i++) {
		int valid = touch_info & (1 << (3 + i));

		if (valid) {
			int width = finger[3] & 0x0f;
			int height = (finger[3] & 0xf0) >> 4;
			int pressure = finger[4] + elan_tp_params.pressure_adj;
			pressure = DIV_ROUND_NEAREST(pressure * pressure_mult,
						     pressure_div);

			width = MIN(4095, width * elan_tp_params.width_x);
			height = MIN(4095, height * elan_tp_params.width_y);
			pressure = MIN(1023, pressure);

			report.finger[ri].confidence = 1;
			report.finger[ri].tip = 1;
			report.finger[ri].inrange = 1;
			report.finger[ri].id = i;
			report.finger[ri].width = width;
			report.finger[ri].height = height;
			report.finger[ri].x = ((finger[0] & 0xf0) << 4) |
					      finger[1];
			report.finger[ri].y =
				elan_tp_params.max_y -
				(((finger[0] & 0x0f) << 8) | finger[2]);
			report.finger[ri].pressure = pressure;
			finger += ETP_FINGER_DATA_LEN;
			ri++;
			finger_status[i] = 1;
		} else if (finger_status[i]) {
			report.finger[ri].id = i;
			/* When a finger is leaving, it's not a plam */
			report.finger[ri].confidence = 1;
			ri++;
			finger_status[i] = 0;
		}
	}

	report.count = ri;
	report.timestamp = timestamp;

	if (touch_info & 0x01) {
		/* Do not report zero-finger click events */
		if (report.count > 0)
			report.button = 1;
	}

	if (hover_info & 0x40) {
		/* TODO(b/35582031): Report hover event */
		CPRINTF("[TP] hover!\n");
	}

	set_touchpad_report(&report);

	return 0;
}

static void elan_get_fwinfo(void)
{
	uint16_t ic_type = elan_tp_params.ic_type;
	uint16_t iap_version = elan_tp_params.iap_version;

	switch (ic_type) {
	case 0x09:
		elan_tp_params.page_count = 768;
		break;
	case 0x0D:
		elan_tp_params.page_count = 896;
		break;
	case 0x00:
	case 0x10:
	case 0x14:
	case 0x15:
		elan_tp_params.page_count = 1024;
		break;
	default:
		elan_tp_params.page_count = -1;
		CPRINTS("unknown ic_type: %d", ic_type);
	}

	if ((ic_type == 0x14 || ic_type == 0x15) && iap_version >= 2) {
		elan_tp_params.page_count /= 8;
		elan_tp_params.page_size = 512;
	} else if (ic_type >= 0x0D && iap_version >= 1) {
		elan_tp_params.page_count /= 2;
		elan_tp_params.page_size = 128;
	} else {
		elan_tp_params.page_size = 64;
	}
}

/*
 * - dpi == logical dimension / physical dimension (inches)
 *   (254 tenths of mm per inch)
 */
__maybe_unused static int calc_physical_dimension(int dpi, int logical_dim)
{
	return round_divide(254 * logical_dim, dpi);
}

static int elan_i2c_get_pattern(void)
{
	int rv;
	uint8_t val[2];

	rv = elan_tp_read_cmd(ETP_I2C_PATTERN_CMD, (uint16_t *)val);
	if (rv) {
		CPRINTS("%s: read pattern failed", __func__);
		return rv;
	}
	CPRINTS("%s: read pattern reg:%04X.", __func__, elan_tp_params.pattern);

	/*
	 * Not all versions of firmware implement "get pattern" command. When
	 * this command is not implemented the device will respond with 0xFF
	 * 0xFF, which we will treat as "old" pattern 0.
	 */
	elan_tp_params.pattern = (*(uint16_t *)val == 0xFFFF) ? 0 : val[1];

	return EC_SUCCESS;
}

static int elan_query_product(void)
{
	int rv;
	uint8_t val[2];

	rv = elan_i2c_get_pattern();
	if (rv) {
		return rv;
	}

	if (elan_tp_params.pattern >= 0x01) {
		rv = elan_tp_read_cmd(ETP_I2C_IC_TYPE_CMD, (uint16_t *)val);
		if (rv) {
			return rv;
		}
#ifdef CONFIG_ZEPHYR
		elan_tp_params.ic_type = sys_be16_to_cpu(*(uint16_t *)val);
#else
		elan_tp_params.ic_type = be16toh(*(uint16_t *)val);
#endif
	} else {
		rv = elan_tp_read_cmd(ETP_I2C_IC_TYPE_P0_CMD, (uint16_t *)val);
		if (rv) {
			return rv;
		}
		elan_tp_params.ic_type = val[0];
	}
	CPRINTS("%s: ic_type:%04X.", __func__, elan_tp_params.ic_type);

	if (elan_tp_params.pattern >= 0x01) {
		rv = elan_tp_read_cmd(ETP_I2C_IAP_VERSION_CMD, (uint16_t *)val);
		if (rv) {
			return rv;
		}
		elan_tp_params.iap_version = val[1];
	} else {
		rv = elan_tp_read_cmd(ETP_I2C_IAP_VERSION_P0_CMD,
				      (uint16_t *)val);
		if (rv) {
			return rv;
		}
		elan_tp_params.iap_version = val[0];
	}
	CPRINTS("%s: iap_version:%04X.", __func__, elan_tp_params.iap_version);

	return EC_SUCCESS;
}

/* Initialize the controller ICs after reset */
test_export_static void elan_tp_init(void)
{
	int rv;
	uint8_t val[2];
	int dpi_x, dpi_y;

	CPRINTS("%s", __func__);

	elan_tp_write_cmd(ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	crec_msleep(100);
	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR_FLAGS,
		      NULL, 0, val, sizeof(val));

	CPRINTS("reset rv %d buf=%04x", rv, *((uint16_t *)val));
	if (rv)
		goto out;

	/*
	 * Read pattern , then based on pattern to determine what command to
	 * send to get IC type, IAP version, etc
	 */
	rv = elan_query_product();
	if (rv)
		goto out;

	elan_get_fwinfo();

	/* Read min/max */
	rv = elan_tp_read_cmd(ETP_I2C_MAX_X_AXIS_CMD, &elan_tp_params.max_x);
	if (rv)
		goto out;
	rv = elan_tp_read_cmd(ETP_I2C_MAX_Y_AXIS_CMD, &elan_tp_params.max_y);
	if (rv)
		goto out;

	/* Read min/max */
	rv = elan_tp_read_cmd(ETP_I2C_XY_TRACENUM_CMD, (uint16_t *)val);
	if (rv)
		goto out;
	if (val[0] == 0 || val[1] == 0) {
		CPRINTS("Invalid XY_TRACENUM");
		goto out;
	}

	/* ETP_FWIDTH_REDUCE reduces the apparent width to avoid treating large
	 * finger as palm. Multiply value by 2 as HID multitouch divides it.
	 */
	elan_tp_params.width_x =
		2 * ((elan_tp_params.max_x / val[0]) - ETP_FWIDTH_REDUCE);
	elan_tp_params.width_y =
		2 * ((elan_tp_params.max_y / val[1]) - ETP_FWIDTH_REDUCE);

	rv = elan_tp_read_cmd(ETP_I2C_PRESSURE_CMD, (uint16_t *)val);
	if (rv)
		goto out;
	elan_tp_params.pressure_adj = (val[0] & 0x10) ? 0 : ETP_PRESSURE_OFFSET;

	rv = elan_tp_read_cmd(ETP_I2C_RESOLUTION_CMD, (uint16_t *)val);
	if (rv)
		goto out;

	if (elan_tp_params.pattern <= 0x01) {
		dpi_x = 10 * val[0] + 790;
		dpi_y = 10 * val[1] + 790;
	} else {
		dpi_x = (val[0] + 3) * 100;
		dpi_y = (val[1] + 3) * 100;
	}

	CPRINTS("max=%d/%d width=%d/%d adj=%d dpi=%d/%d", elan_tp_params.max_x,
		elan_tp_params.max_y, elan_tp_params.width_x,
		elan_tp_params.width_y, elan_tp_params.pressure_adj, dpi_x,
		dpi_y);

#if defined(CONFIG_USB_HID_TOUCHPAD) || \
	defined(CONFIG_PLATFORM_EC_ONE_WIRE_UART_KEYBOARD)
	/* Validity check dimensions provided at build time. */
	if (elan_tp_params.max_x != CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X ||
	    elan_tp_params.max_y != CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y ||
	    calc_physical_dimension(dpi_x, elan_tp_params.max_x) !=
		    CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X ||
	    calc_physical_dimension(dpi_y, elan_tp_params.max_y) !=
		    CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y) {
		CPRINTS("*** TP mismatch!");
	}
#endif

	/* Switch to absolute mode */
	rv = elan_tp_write_cmd(ETP_I2C_SET_CMD, ETP_ENABLE_ABS);
	if (rv)
		goto out;

	/* Sleep control off */
	rv = elan_tp_write_cmd(ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);

	/* Enable interrupt to fetch reports */
	gpio_enable_interrupt(GPIO_TOUCHPAD_INT);

out:
	CPRINTS("%s:%d", __func__, rv);

	return;
}
DECLARE_DEFERRED(elan_tp_init);

#if defined(CONFIG_USB_UPDATE) || defined(CONFIG_TEST)
int touchpad_get_info(struct touchpad_info *tp)
{
	int rv;
	uint16_t val;

	tp->status = EC_RES_SUCCESS;
	tp->vendor = ELAN_VENDOR_ID;

	/* Get unique ID, FW, SM version. */
	rv = elan_tp_read_cmd(ETP_I2C_UNIQUEID_CMD, &val);
	if (rv)
		return -1;
	tp->elan.id = val;

	rv = elan_tp_read_cmd(ETP_I2C_FW_VERSION_CMD, &val);
	if (rv)
		return -1;
	tp->elan.fw_version = val & 0xff;

	rv = elan_tp_read_cmd(ETP_I2C_FW_CHECKSUM_CMD, &val);
	if (rv)
		return -1;
	tp->elan.fw_checksum = val;

	return sizeof(*tp);
}

static int elan_in_main_mode(void)
{
	uint16_t val;

	elan_tp_read_cmd(ETP_I2C_IAP_CTRL_CMD, &val);
	return val & ETP_I2C_MAIN_MODE_ON;
}

static int elan_read_write_iap_type(void)
{
	for (int retry = 0; retry < 3; ++retry) {
		uint16_t val;

		if (elan_tp_write_cmd(ETP_I2C_IAP_TYPE_CMD,
				      elan_tp_params.page_size / 2))
			return EC_ERROR_UNKNOWN;

		if (elan_tp_read_cmd(ETP_I2C_IAP_TYPE_CMD, &val))
			return EC_ERROR_UNKNOWN;

		if (val == elan_tp_params.page_size / 2)
			return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static int elan_prepare_for_update(void)
{
	uint16_t rx_buf;
	int initial_mode;

	initial_mode = elan_in_main_mode();
	if (!initial_mode) {
		CPRINTS("%s: In IAP mode, reset IC.", __func__);
		elan_tp_write_cmd(ETP_I2C_IAP_RESET_CMD, ETP_I2C_IAP_RESET);
		crec_msleep(30);
	}
	/* Send the passphrase */
	elan_tp_write_cmd(ETP_I2C_IAP_CMD, ETP_I2C_IAP_PASSWORD);
	crec_msleep(initial_mode ? 100 : 30);

	/* We should be in the IAP mode now */
	if (elan_in_main_mode()) {
		CPRINTS("%s: Failure to enter IAP mode.", __func__);
		return EC_ERROR_UNKNOWN;
	}

	if (elan_tp_params.ic_type >= 0x0D && elan_tp_params.iap_version >= 1) {
		if (elan_read_write_iap_type())
			return EC_ERROR_UNKNOWN;
	}

	/* Send the passphrase again */
	elan_tp_write_cmd(ETP_I2C_IAP_CMD, ETP_I2C_IAP_PASSWORD);
	crec_msleep(30);

	/* Verify the password */
	if (elan_tp_read_cmd(ETP_I2C_IAP_CMD, &rx_buf)) {
		CPRINTS("%s: Cannot read IAP password.", __func__);
		return EC_ERROR_UNKNOWN;
	}
	if (rx_buf != ETP_I2C_IAP_PASSWORD) {
		CPRINTS("%s: Got an unexpected IAP password %0x4x.", __func__,
			rx_buf);
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int touchpad_update_page(const uint8_t *data)
{
	const uint8_t cmd[2] = { ETP_I2C_IAP_REG_L, ETP_I2C_IAP_REG_H };
	uint16_t checksum = 0;
	uint16_t rx_buf;
	int i, rv;

	for (i = 0; i < elan_tp_params.page_size; i += 2)
		checksum += ((uint16_t)(data[i + 1]) << 8) | (data[i]);

#ifdef CONFIG_ZEPHYR
	checksum = sys_cpu_to_le16(checksum);
#else
	checksum = htole16(checksum);
#endif

	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 1);

	rv = i2c_xfer_unlocked(CONFIG_TOUCHPAD_I2C_PORT,
			       CONFIG_TOUCHPAD_I2C_ADDR_FLAGS, cmd, sizeof(cmd),
			       NULL, 0, I2C_XFER_START);
	if (rv)
		goto fail;
	rv = i2c_xfer_unlocked(CONFIG_TOUCHPAD_I2C_PORT,
			       CONFIG_TOUCHPAD_I2C_ADDR_FLAGS, data,
			       elan_tp_params.page_size, NULL, 0, 0);
	if (rv)
		goto fail;
	rv = i2c_xfer_unlocked(CONFIG_TOUCHPAD_I2C_PORT,
			       CONFIG_TOUCHPAD_I2C_ADDR_FLAGS,
			       (uint8_t *)&checksum, sizeof(checksum), NULL, 0,
			       I2C_XFER_STOP);
	if (rv)
		goto fail;

fail:
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 0);
	if (rv)
		return rv;
	crec_msleep(elan_tp_params.page_size >= 512 ? 50 : 35);

	rv = elan_tp_read_cmd(ETP_I2C_IAP_CTRL_CMD, &rx_buf);

	if (rv || (rx_buf & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR))) {
		CPRINTS("%s: IAP reports failed write : %x.", __func__, rx_buf);
		return EC_ERROR_UNKNOWN;
	}
	return 0;
}

int touchpad_update_write(int offset, int size, const uint8_t *data)
{
	static int iap_addr = -1;
	int addr, rv;

	CPRINTS("%s %08x %d", __func__, offset, size);

	if (offset == 0) {
		/* Verify the IC type is aligned with defined firmware size */
		if (elan_tp_params.page_size * elan_tp_params.page_count !=
		    FW_SIZE) {
			CPRINTS("%s: IC(%d*%d) size and FW_SIZE(%d) mismatch",
				__func__, elan_tp_params.page_count,
				elan_tp_params.page_size, FW_SIZE);
			return EC_ERROR_UNKNOWN;
		}

		gpio_disable_interrupt(GPIO_TOUCHPAD_INT);
		CPRINTS("%s: prepare fw update.", __func__);
		rv = elan_prepare_for_update();
		if (rv)
			return rv;
		iap_addr = 0;
	}

	if (offset <= (ETP_IAP_START_ADDR * 2) &&
	    (ETP_IAP_START_ADDR * 2) < (offset + size)) {
		iap_addr = ((data[ETP_IAP_START_ADDR * 2 - offset + 1] << 8) |
			    data[ETP_IAP_START_ADDR * 2 - offset])
			   << 1;
		CPRINTS("%s: payload starts from 0x%x.", __func__, iap_addr);
	}

	/* Data that comes in must align with page_size */
	if (offset % elan_tp_params.page_size)
		return EC_ERROR_INVAL;

	for (addr = offset; addr < (offset + size);
	     addr += elan_tp_params.page_size) {
		if (iap_addr > addr) /* Skip chunk */
			continue;
		rv = touchpad_update_page(data + addr - offset);
		if (rv)
			return rv;
		CPRINTF("/p%d", addr / elan_tp_params.page_size);
		watchdog_reload();
	}
	CPRINTF("\n");

	if (offset + size == FW_SIZE) {
		CPRINTS("%s: End update, wait for reset.", __func__);
		hook_call_deferred(&elan_tp_init_data, 600 * MSEC);
	}
	return EC_SUCCESS;
}

/* Debugging mode. */

/* Allowed debug commands. We only store a hash of the allowed commands. */
#define TOUCHPAD_ELAN_DEBUG_CMD_LENGTH 50
#define TOUCHPAD_ELAN_DEBUG_NUM_CMD 3

test_mockable_static const uint8_t allowed_command_hashes
	[TOUCHPAD_ELAN_DEBUG_NUM_CMD][SHA256_DIGEST_SIZE] = {
		{ 0x0a, 0xf6, 0x37, 0x03, 0x93, 0xb2, 0xde, 0x8c,
		  0x56, 0x7b, 0x86, 0xba, 0xa6, 0x79, 0xe3, 0xa3,
		  0x8b, 0xc7, 0x15, 0xf2, 0x53, 0xcf, 0x71, 0x8b,
		  0x3d, 0xe4, 0x81, 0xf9, 0xd9, 0xa8, 0x78, 0x48 },
		{ 0xac, 0xe5, 0xbf, 0x17, 0x1f, 0xde, 0xce, 0x76,
		  0x0c, 0x0e, 0xf8, 0xa2, 0xe9, 0x67, 0x2d, 0xc9,
		  0x1b, 0xd4, 0xba, 0x34, 0x51, 0xca, 0xf6, 0x6d,
		  0x7b, 0xb2, 0x1f, 0x14, 0x82, 0x1c, 0x0b, 0x74 },
		{ 0xa2, 0xa5, 0x0a, 0xf3, 0x79, 0xb6, 0x81, 0x61,
		  0x56, 0x3f, 0x89, 0x46, 0xbe, 0x38, 0x43, 0xf7,
		  0x8a, 0x68, 0xd1, 0xd5, 0x4c, 0x1a, 0x86, 0x52,
		  0x89, 0x0c, 0x01, 0x21, 0x0e, 0x2b, 0xa5, 0x35 },
	};

/* Debugging commands need to allocate a <=1k buffer. */
SHARED_MEM_CHECK_SIZE(1024);

int touchpad_debug(const uint8_t *param, unsigned int param_size,
		   uint8_t **data, unsigned int *data_size)
{
	static uint8_t *buffer;
	static unsigned int buffer_size;
	unsigned int offset;

	/* Offset parameter is 1 byte. */
	if (param_size < 1)
		return EC_RES_INVALID_PARAM;

	/*
	 * Debug command, compute SHA-256, check that it matches allowed hashes,
	 * and execute I2C command.
	 *
	 * param[0] must be 0xff
	 * param[1] is the offset of the command in the data
	 * param[2] is the command length
	 * param[3-4] is the read-back length (MSB first), can be 0
	 * param[5-49] is verified using SHA-256 hash.
	 */
	if (param[0] == 0xff && param_size == TOUCHPAD_ELAN_DEBUG_CMD_LENGTH) {
		struct sha256_ctx ctx;
		uint8_t *command_hash;
		unsigned int offset = param[1];
		unsigned int write_length = param[2];
		unsigned int read_length = ((unsigned int)param[3] << 8) |
					   param[4];
		int i;
		int match;
		int rv;

		if (offset < 5 || write_length == 0 ||
		    (offset + write_length) >= TOUCHPAD_ELAN_DEBUG_CMD_LENGTH)
			return EC_RES_INVALID_PARAM;

		SHA256_init(&ctx);
		SHA256_update(&ctx, param + 5,
			      TOUCHPAD_ELAN_DEBUG_CMD_LENGTH - 5);
		command_hash = SHA256_final(&ctx);

		match = 0;
		for (i = 0; i < TOUCHPAD_ELAN_DEBUG_NUM_CMD; i++) {
			if (!memcmp(command_hash, allowed_command_hashes[i],
				    sizeof(allowed_command_hashes[i]))) {
				match = 1;
				break;
			}
		}

		if (!match)
			return EC_RES_INVALID_PARAM;

		if (buffer) {
			shared_mem_release(buffer);
			buffer = NULL;
		}

		buffer_size = read_length;

		if (read_length > 0) {
			if (shared_mem_acquire(buffer_size, (char **)&buffer) !=
			    EC_SUCCESS) {
				buffer = NULL;
				buffer_size = 0;
				return EC_RES_BUSY;
			}

			memset(buffer, 0, buffer_size);
		}

		rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT,
			      CONFIG_TOUCHPAD_I2C_ADDR_FLAGS, &param[offset],
			      write_length, buffer, read_length);

		if (rv)
			return EC_RES_BUS_ERROR;

		return EC_RES_SUCCESS;
	}

	/*
	 * Data request: Retrieve previously read data from buffer, in blocks of
	 * 64 bytes.
	 */
	offset = param[0] * 64;

	if (!buffer)
		return EC_RES_UNAVAILABLE;

	if (offset >= buffer_size) {
		shared_mem_release(buffer);
		buffer = NULL;
		*data = NULL;
		*data_size = 0;
		return EC_RES_OVERFLOW;
	}

	*data = buffer + offset;
	*data_size = MIN(64, buffer_size - offset);

	return EC_RES_SUCCESS;
}
#endif

/*
 * Try to read touchpad report up to 3 times, reset the touchpad if we still
 * fail.
 */
void elan_tp_read_report_retry(void)
{
	int ret;
	int retry = 3;

	while (retry--) {
		ret = elan_tp_read_report();

		if (ret <= 0)
			return;

		/* Try again */
		crec_msleep(1);
	}

	/* Failed to read data, reset the touchpad. */
	CPRINTF("Resetting TP.\n");
	board_touchpad_reset();
	elan_tp_init();
}

void touchpad_interrupt(enum gpio_signal signal)
{
	irq_ts = __hw_clock_source_read();

	task_wake(TASK_ID_TOUCHPAD);
}

/* Make a decision on touchpad power, based on USB and tablet mode status. */
static void touchpad_power_control(void)
{
	static int enabled = 1;
	int enable = 1;

#ifdef CONFIG_USB_SUSPEND
	enable = enable &&
		 (!usb_is_suspended() || usb_is_remote_wakeup_enabled());
#endif

#ifdef CONFIG_TABLET_MODE
	enable = enable && !tablet_get_mode();
#endif

	if (enabled == enable)
		return;

	elan_tp_set_power(enable);

	enabled = enable;
}

void touchpad_task(void *u)
{
	uint32_t event;

	elan_tp_init();
	touchpad_power_control();

	while (1) {
		event = task_wait_event(-1);

		if (event & TASK_EVENT_WAKE)
			elan_tp_read_report_retry();

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
