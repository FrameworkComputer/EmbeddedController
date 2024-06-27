/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <libusb.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Command line options */
static uint16_t vid = 0x18d1; /* Google */
static uint16_t pid = 0x5022; /* Hammer */
static uint16_t rsize = 637; /* Read size */
typedef struct {
	uint8_t addr; /* Endpoint address */
	uint8_t len; /* Max. packet size */
} ep_info_t;
static ep_info_t in_ep;
static ep_info_t out_ep;
static uint8_t extended_i2c_exercise; /* non-zero to exercise */
static char *firmware_binary = "144.0_2.0.bin"; /* firmware blob */

#define USB_I2C_SUBCLASS 0x52
#define USB_I2C_PROTOCOL 0x01
/* Firmware binary blob related */
#define MAX_FW_PAGE_SIZE 512
#define MAX_FW_PAGE_COUNT 1024
#define MAX_FW_SIZE (128 * 1024)
#define I2C_RESPONSE_OFFSET 4

#define SAFE_FREE(p)      \
	if (p) {          \
		free(p);  \
		p = NULL; \
	}

static uint8_t fw_data[MAX_FW_SIZE];
int fw_page_count;
int fw_page_size;
int fw_size;
uint16_t ic_type;
int iap_version;

/* Utility functions */
static int le_bytes_to_int(uint8_t *buf)
{
	return buf[0] + (int)(buf[1] << 8);
}

static int be_bytes_to_int(uint8_t *buf)
{
	return (int)(buf[0] << 8) + buf[1];
}

/* Command line parsing related */
static char *progname;
static char *short_opts = ":f:v:p:r:hd";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{ "file", 1, NULL, 'f' }, { "vid", 1, NULL, 'v' },
	{ "pid", 1, NULL, 'p' },  { "rsize", 1, NULL, 'r' },
	{ "help", 0, NULL, 'h' }, { "debug", 0, NULL, 'd' },
	{ NULL, 0, NULL, 0 },
};

static void usage(int errs)
{
	printf("\nUsage: %s [options]\n"
	       "\n"
	       "Firmware updater over USB for trackpad under hammer\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "  -f,--file   STR         Firmware binary (default %s)\n"
	       "  -v,--vid    HEXVAL      Vendor ID (default %04x)\n"
	       "  -p,--pid    HEXVAL      Product ID (default %04x)\n"
	       "  -r,--rsize  VAL         Read Size (default %d)\n"
	       "  -d,--debug              Exercise extended read I2C over USB\n"
	       "                          and print verbose debug messages.\n"
	       "  -h,--help               Show this message\n"
	       "\n",
	       progname, firmware_binary, vid, pid, rsize);

	exit(!!errs);
}

static void parse_cmdline(int argc, char *argv[])
{
	char *e = 0;
	int i, errorcnt = 0;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	opterr = 0; /* quiet, you */
	while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
		switch (i) {
		case 'f':
			firmware_binary = optarg;
			break;
		case 'p':
			pid = (uint16_t)strtoull(optarg, &e, 16);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'v':
			vid = (uint16_t)strtoull(optarg, &e, 16);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'r':
			rsize = (uint16_t)strtoull(optarg, &e, 0);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'd':
			extended_i2c_exercise = 1;
			break;
		case 'h':
			usage(errorcnt);
			break;
		case 0: /* auto-handled option */
			break;
		case '?':
			if (optopt)
				printf("Unrecognized option: -%c\n", optopt);
			else
				printf("Unrecognized option: %s\n",
				       argv[optind - 1]);
			errorcnt++;
			break;
		case ':':
			printf("Missing argument to %s\n", argv[optind - 1]);
			errorcnt++;
			break;
		default:
			printf("Internal error at %s:%d\n", __FILE__, __LINE__);
			exit(1);
		}
	}

	if (errorcnt)
		usage(errorcnt);
}

/* USB transfer related */
static uint8_t *rx_buf;
static uint8_t tx_buf[1024];

static struct libusb_device_handle *devh;
static struct libusb_transfer *rx_transfer;
static struct libusb_transfer *tx_transfer;

static int claimed_iface;
static int iface_num = -1;
static int do_exit;

/* I2C related */
static int bus_type = -1;
static int i2c_devnum;
static int i2c_addr;

static void request_exit(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	do_exit++; /* Why need this ? */

	if (tx_transfer)
		libusb_free_transfer(tx_transfer);
	if (rx_transfer)
		libusb_free_transfer(rx_transfer);
	if (devh) {
		if (claimed_iface)
			libusb_release_interface(devh, iface_num);
		libusb_close(devh);
	}
	libusb_exit(NULL);

	SAFE_FREE(rx_buf);

	exit(1);
}

#define DIE(msg, r) \
	request_exit("%s: line %d, %s\n", msg, __LINE__, libusb_error_name(r))

static void sighandler(int signum)
{
	request_exit("caught signal %d: %s\n", signum, strsignal(signum));
}

static int find_endpoints()
{
	int iface_num = -1;
	int r, i, j, k;
	struct libusb_device *dev;
	struct libusb_config_descriptor *conf = 0;
	const struct libusb_interface *iface0;
	const struct libusb_interface_descriptor *iface;
	const struct libusb_endpoint_descriptor *ep;

	dev = libusb_get_device(devh);
	r = libusb_get_active_config_descriptor(dev, &conf);
	if (r < 0) {
		DIE("get_active_config", r);
		return -1;
	}

	for (i = 0; i < conf->bNumInterfaces; i++) {
		iface0 = &conf->interface[i];
		for (j = 0; j < iface0->num_altsetting; j++) {
			iface = &iface0->altsetting[j];
			if (iface->bInterfaceClass != 0xFF ||
			    iface->bInterfaceSubClass != USB_I2C_SUBCLASS ||
			    iface->bInterfaceProtocol != USB_I2C_PROTOCOL) {
				continue;
			}
			for (k = 0; k < iface->bNumEndpoints; k++) {
				ep = &iface->endpoint[k];
				if ((ep->bEndpointAddress &
				     LIBUSB_ENDPOINT_DIR_MASK) ==
				    LIBUSB_ENDPOINT_IN) {
					in_ep.addr = ep->bEndpointAddress;
					in_ep.len = ep->wMaxPacketSize;
				} else {
					out_ep.addr = ep->bEndpointAddress;
					out_ep.len = ep->wMaxPacketSize;
				}
				iface_num = i;
			}
			break;
		}
	}

	libusb_free_config_descriptor(conf);
	return iface_num;
}

static void init_with_libusb(void)
{
	int r = 1;

	printf("init usb interface\n");
	r = libusb_init(NULL);
	if (r < 0)
		DIE("init", r);

	printf("open_device %04x:%04x\n", vid, pid);
	devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!devh)
		request_exit("can't find device\n");

	iface_num = find_endpoints();
	if (iface_num < 0)
		request_exit("can't find interface");

	printf("claim_interface %d to use IN ep 0x%x and OUT ep 0x%x\n",
	       iface_num, in_ep.addr, out_ep.addr);
	r = libusb_claim_interface(devh, iface_num);
	if (r < 0)
		DIE("claim interface", r);
	claimed_iface = 1;
}

static void register_sigaction(void)
{
	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
}

/* Transfer over libusb */
#define I2C_PORT_ON_HAMMER 0x00
#define I2C_ADDRESS_ON_HAMMER 0x15

static int check_read_status(int r, int expected, int actual)
{
	int i;
	if (r)
		printf("Warning: libusb_bulk_transfer return error : %d\n", r);
	if (actual != (expected + 4)) {
		printf("Warning: Not reading back %d bytes.\n", expected);
		r = 1;
	}

	/* Check transaction status as defined in usb_i2c.h */
	for (i = 0; i < 4; ++i)
		if (rx_buf[i] != 0)
			break;

	if (i != 4) {
		r = le_bytes_to_int(rx_buf);
		printf("Warning: Defined error code (%d) returned.\n", r);
	}

	if (r || extended_i2c_exercise) {
		printf("\nDumping the receive buffer:\n");
		printf("  Recv %d bytes from USB hosts.\n", actual);
		for (i = 0; i < actual; ++i)
			printf("    [%2d]bytes: 0x%0x\n", i, rx_buf[i]);
	}
	return r;
}

#define MAX_USB_PACKET_SIZE 64
#define PRIMITIVE_READING_SIZE 60

static int i2c_single_write_and_read(const uint8_t *to_write,
				     uint16_t write_length, uint8_t *to_read,
				     uint16_t read_length)
{
	return -1;
}

static int libusb_single_write_and_read(const uint8_t *to_write,
					uint16_t write_length, uint8_t *to_read,
					uint16_t read_length)
{
	int r;
	int tx_ready;
	int remains;
	int sent_bytes = 0;
	int actual_length = -1;
	int offset = read_length > PRIMITIVE_READING_SIZE ? 6 : 4;
	tx_transfer = rx_transfer = 0;

	memmove(tx_buf + offset, to_write, write_length);
	tx_buf[0] = I2C_PORT_ON_HAMMER | ((write_length >> 8) << 4);
	tx_buf[1] = I2C_ADDRESS_ON_HAMMER;
	tx_buf[2] = write_length & 0xff;
	if (read_length > PRIMITIVE_READING_SIZE) {
		tx_buf[3] = (read_length & 0x7f) | (1 << 7);
		tx_buf[4] = read_length >> 7;
		if (extended_i2c_exercise) {
			printf("Triggering extended reading."
			       "rc:%0x, rc1:%0x\n",
			       tx_buf[3], tx_buf[4]);
			printf("Expecting %d Bytes.\n",
			       (tx_buf[3] & 0x7f) | (tx_buf[4] << 7));
		}
	} else {
		tx_buf[3] = read_length;
	}

	/*
	 * TODO: This loop is probably not required as we write the whole block
	 * in one transaction.
	 */
	while (sent_bytes < (offset + write_length)) {
		tx_ready = remains = (offset + write_length) - sent_bytes;

		r = libusb_bulk_transfer(devh, out_ep.addr, tx_buf + sent_bytes,
					 tx_ready, &actual_length, 5000);
		if (r == 0 && actual_length == tx_ready) {
			int rx_len = 0;

			actual_length = 0;
			do {
				r = libusb_bulk_transfer(devh, in_ep.addr,
							 rx_buf + actual_length,
							 rsize, &rx_len, 5000);
				if (r) {
					break;
				}
				actual_length += rx_len;
				usleep(100 * 1000);
			} while ((read_length + 4) != actual_length);
		}
		r = check_read_status(r,
				      (remains == tx_ready) ? read_length : 0,
				      actual_length);
		if (r)
			break;
		sent_bytes += tx_ready;
	}
	return r;
}

static int single_write_and_read(const uint8_t *to_write, uint16_t write_length,
				 uint8_t *to_read, uint16_t read_length)
{
	if (bus_type == BUS_USB) {
		return libusb_single_write_and_read(to_write, write_length,
						    to_read, read_length);
	}
	if (bus_type == BUS_I2C) {
		return i2c_single_write_and_read(to_write, write_length,
						 to_read, read_length);
	}
	return -1;
}

/* Control Elan trackpad I2C over USB */
#define ETP_I2C_INF_LENGTH 2

static int elan_write_and_read(int reg, uint8_t *buf, int read_length,
			       int with_cmd, int cmd)
{
	tx_buf[0] = (reg >> 0) & 0xff;
	tx_buf[1] = (reg >> 8) & 0xff;
	if (with_cmd) {
		tx_buf[2] = (cmd >> 0) & 0xff;
		tx_buf[3] = (cmd >> 8) & 0xff;
	}
	return single_write_and_read(tx_buf, with_cmd ? 4 : 2, rx_buf,
				     read_length);
}

static int elan_read_block(int reg, uint8_t *buf, int read_length)
{
	return elan_write_and_read(reg, buf, read_length, 0, 0);
}

static int elan_read_cmd(int reg)
{
	return elan_read_block(reg, rx_buf, ETP_I2C_INF_LENGTH);
}

static int elan_write_cmd(int reg, int cmd)
{
	return elan_write_and_read(reg, rx_buf, 0, 1, cmd);
}

/* Elan trackpad firmware information related */
#define ETP_I2C_PATTERN_CMD 0x0100
#define ETP_I2C_IC_TYPE_CMD 0x0103
#define ETP_I2C_IAP_VERSION_CMD 0x0110
#define ETP_I2C_IC_TYPE_P0_CMD 0x0110
#define ETP_I2C_IAP_VERSION_P0_CMD 0x0111
#define ETP_I2C_FW_VERSION_CMD 0x0102
#define ETP_I2C_IAP_CHECKSUM_CMD 0x0315
#define ETP_I2C_FW_CHECKSUM_CMD 0x030F

static void elan_get_fw_info(void)
{
	switch (ic_type) {
	case 0x09:
		fw_page_count = 768;
		break;
	case 0x0D:
		fw_page_count = 896;
		break;
	case 0x00:
	case 0x10:
	case 0x14:
	case 0x15:
		fw_page_count = 1024;
		break;
	default:
		request_exit("The IC type is not supported.\n");
	}

	if ((ic_type == 0x14 || ic_type == 0x15) && iap_version >= 2) {
		fw_page_count /= 8;
		fw_page_size = 512;
	} else if (ic_type >= 0x0D && iap_version >= 1) {
		fw_page_count /= 2;
		fw_page_size = 128;
	} else {
		fw_page_size = 64;
	}
}

static int elan_get_checksum(int is_iap)
{
	elan_read_cmd(is_iap ? ETP_I2C_IAP_CHECKSUM_CMD :
			       ETP_I2C_FW_CHECKSUM_CMD);
	return le_bytes_to_int(rx_buf + 4);
}

static int elan_i2c_get_pattern(void)
{
	if (elan_read_cmd(ETP_I2C_PATTERN_CMD) != 0) {
		return -1;
	}

	/*
	 * Not all versions of firmware implement "get pattern" command. When
	 * this command is not implemented the device will respond with 0xFFFF,
	 * which we will treat as "old" pattern 0.
	 */
	int response = le_bytes_to_int(rx_buf + I2C_RESPONSE_OFFSET);

	return (response == 0xFFFF) ? 0 : rx_buf[1 + I2C_RESPONSE_OFFSET];
}

static void elan_query_product(void)
{
	int pattern = elan_i2c_get_pattern();

	if (pattern == -1) {
		request_exit("Failed to read ELAN device pattern");
	}
	printf("Pattern of ELAN touchpad: %04X\n", pattern);

	if (pattern >= 0x01) {
		if (elan_read_cmd(ETP_I2C_IC_TYPE_CMD) != 0) {
			request_exit("Failed to read IC type");
		}
		ic_type = be_bytes_to_int(rx_buf + I2C_RESPONSE_OFFSET);

		if (elan_read_cmd(ETP_I2C_IAP_VERSION_CMD)) {
			request_exit("Failed to read IAP version");
		}
		iap_version = rx_buf[1 + I2C_RESPONSE_OFFSET];
	} else {
		if (elan_read_cmd(ETP_I2C_IC_TYPE_P0_CMD) != 0) {
			request_exit("Failed to read IC type");
		}
		ic_type = rx_buf[0 + I2C_RESPONSE_OFFSET];

		if (elan_read_cmd(ETP_I2C_IAP_VERSION_P0_CMD)) {
			request_exit("Failed to read IAP version");
		}
		iap_version = rx_buf[0 + I2C_RESPONSE_OFFSET];
	}
}

/* Update preparation */
#define ETP_I2C_IAP_RESET_CMD 0x0314
#define ETP_I2C_IAP_RESET 0xF0F0
#define ETP_I2C_IAP_CTRL_CMD 0x0310
#define ETP_I2C_MAIN_MODE_ON (1 << 9)
#define ETP_I2C_IAP_CMD 0x0311
#define ETP_I2C_IAP_PASSWORD 0x1EA5
#define ETP_I2C_IAP_TYPE_CMD 0x0304

static int elan_in_main_mode(void)
{
	elan_read_cmd(ETP_I2C_IAP_CTRL_CMD);
	return le_bytes_to_int(rx_buf + 4) & ETP_I2C_MAIN_MODE_ON;
}

static int elan_read_write_iap_type(void)
{
	for (int retry = 0; retry < 3; ++retry) {
		uint16_t val;

		if (elan_write_cmd(ETP_I2C_IAP_TYPE_CMD, fw_page_size / 2))
			return -1;

		if (elan_read_cmd(ETP_I2C_IAP_TYPE_CMD))
			return -1;

		val = le_bytes_to_int(rx_buf + 4);
		if (val == fw_page_size / 2) {
			printf("%s: OK\n", __func__);
			return 0;
		}
	}
	return -1;
}

static void elan_prepare_for_update(void)
{
	printf("%s\n", __func__);

	int initial_mode = elan_in_main_mode();
	if (!initial_mode) {
		printf("In IAP mode, reset IC.\n");
		elan_write_cmd(ETP_I2C_IAP_RESET_CMD, ETP_I2C_IAP_RESET);
		usleep(30 * 1000);
	}

	/* Send the passphrase */
	elan_write_cmd(ETP_I2C_IAP_CMD, ETP_I2C_IAP_PASSWORD);
	usleep((initial_mode ? 100 : 30) * 1000);

	/* We should be in the IAP mode now */
	if (elan_in_main_mode())
		request_exit("Failure to enter IAP mode, still in main mode\n");

	if (ic_type >= 0x0D && iap_version >= 1) {
		if (elan_read_write_iap_type())
			request_exit("Failure to set IAP mode\n");
	}

	/* Send the passphrase again */
	elan_write_cmd(ETP_I2C_IAP_CMD, ETP_I2C_IAP_PASSWORD);
	usleep(30 * 1000);

	/* Verify the password */
	if (elan_read_cmd(ETP_I2C_IAP_CMD))
		request_exit("cannot read iap password.\n");
	if (le_bytes_to_int(rx_buf + 4) != ETP_I2C_IAP_PASSWORD)
		request_exit("Got an unexpected IAP password %4x\n",
			     le_bytes_to_int(rx_buf + 4));
}

/* Firmware block update */
#define ETP_IAP_START_ADDR 0x0083

static uint16_t elan_calc_checksum(uint8_t *data, int length)
{
	uint16_t checksum = 0;
	for (int i = 0; i < length; i += 2)
		checksum += ((uint16_t)(data[i + 1]) << 8) | (data[i]);
	return checksum;
}

static int elan_get_iap_addr(void)
{
	return le_bytes_to_int(fw_data + ETP_IAP_START_ADDR * 2) * 2;
}

#define ETP_I2C_IAP_REG_L 0x01
#define ETP_I2C_IAP_REG_H 0x06

#define ETP_FW_IAP_PAGE_ERR (1 << 5)
#define ETP_FW_IAP_INTF_ERR (1 << 4)

static int elan_write_fw_block(uint8_t *raw_data, uint16_t checksum)
{
	uint8_t page_store[MAX_FW_PAGE_SIZE + 4];
	int rv;

	page_store[0] = ETP_I2C_IAP_REG_L;
	page_store[1] = ETP_I2C_IAP_REG_H;
	memcpy(page_store + 2, raw_data, fw_page_size);
	page_store[fw_page_size + 2 + 0] = (checksum >> 0) & 0xff;
	page_store[fw_page_size + 2 + 1] = (checksum >> 8) & 0xff;

	rv = single_write_and_read(page_store, fw_page_size + 4, rx_buf, 0);
	if (rv)
		return rv;
	usleep((fw_page_size >= 512 ? 50 : 35) * 1000);
	elan_read_cmd(ETP_I2C_IAP_CTRL_CMD);
	rv = le_bytes_to_int(rx_buf + 4);
	if (rv & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
		printf("IAP reports failed write : %x\n", rv);
		return rv;
	}
	return 0;
}

static uint16_t elan_update_firmware(void)
{
	uint16_t checksum = 0, block_checksum;
	int rv;

	printf("%s\n", __func__);

	for (int i = elan_get_iap_addr(); i < fw_size; i += fw_page_size) {
		printf("\rUpdating page %3d...", i / fw_page_size);
		fflush(stdout);
		block_checksum = elan_calc_checksum(fw_data + i, fw_page_size);
		rv = elan_write_fw_block(fw_data + i, block_checksum);
		if (rv)
			request_exit("Failed to update.\n");
		checksum += block_checksum;
		printf(" Updated, checksum: %d", checksum);
		fflush(stdout);
	}
	return checksum;
}

static void pretty_print_buffer(uint8_t *buf, int len)
{
	int i;

	printf("Buffer = 0x");
	for (i = 0; i < len; ++i)
		printf("%02X", buf[i]);
	printf("\n");
}

static void closefd(int *fd)
{
	if (*fd >= 0)
		close(*fd);
}

static void probe_device(void)
{
	glob_t globbuf;

	if (glob("/dev/hidraw*", 0, NULL, &globbuf) != 0) {
		return;
	}

	for (char **path = globbuf.gl_pathv; *path; path++) {
		__attribute__((cleanup(closefd))) int fd =
			open(*path, O_RDWR | O_NONBLOCK);
		struct hidraw_devinfo info;

		if (fd < 0)
			continue;
		if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0)
			continue;
		if (info.vendor != vid || info.product != pid)
			continue;

		if (info.bustype == BUS_I2C) {
			char phys[256];

			if (ioctl(fd, HIDIOCGRAWPHYS(256), phys) < 0)
				continue;
			if (sscanf(phys, "%d-%04x", &i2c_devnum, &i2c_addr) !=
			    2)
				continue;
		}
		bus_type = info.bustype;

		break;
	}

	globfree(&globbuf);

	return;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	uint16_t local_checksum;
	uint16_t remote_checksum;

	parse_cmdline(argc, argv);

	rx_buf = (uint8_t *)calloc(rsize, sizeof(uint8_t));
	if (!rx_buf) {
		printf("failed to allocate rx buffer\n");
		return -ENOMEM;
	}

	probe_device();
	if (bus_type == BUS_USB) {
		init_with_libusb();
	} else if (bus_type != BUS_I2C) {
		printf("device %04hx:%04hx not found\n", vid, pid);
		ret = -ENODEV;
		goto out;
	}
	register_sigaction();

	/*
	 * Read pattern , then based on pattern to determine what command to
	 * send to get IC type, IAP version, etc
	 */
	elan_query_product();
	elan_get_fw_info();
	fw_size = fw_page_count * fw_page_size;
	printf("FW has %d bytes x %d pages\n", fw_page_size, fw_page_count);

	/* Read the FW file */
	FILE *f = fopen(firmware_binary, "rb");
	if (!f)
		request_exit("Cannot find binary: %s\n", firmware_binary);
	if (fread(fw_data, 1, fw_size, f) != (unsigned int)fw_size)
		request_exit("binary size mismatch, expect %d\n", fw_size);

	/* Trigger an I2C transaction of expecting reading of (rsize - 4) bytes.
	 */
	if (extended_i2c_exercise) {
		tx_buf[0] = 0x05;
		tx_buf[1] = 0x00;
		tx_buf[2] = 0x3C;
		tx_buf[3] = 0x02;
		tx_buf[4] = 0x06;
		tx_buf[5] = 0x00;
		single_write_and_read(tx_buf, 6, rx_buf,
				      rsize - I2C_RESPONSE_OFFSET);
		pretty_print_buffer(rx_buf, rsize);
	}

	/* Get the trackpad ready for receiving update */
	elan_prepare_for_update();

	local_checksum = elan_update_firmware();
	/* Wait for a reset */
	usleep(600 * 1000);
	remote_checksum = elan_get_checksum(1);
	if (remote_checksum != local_checksum)
		printf("checksum diff local=[%04X], remote=[%04X]\n",
		       local_checksum, remote_checksum);

	/* Print the updated firmware information */
	elan_get_fw_info();

out:
	SAFE_FREE(rx_buf);

	return ret;
}
