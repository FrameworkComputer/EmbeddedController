/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <libusb.h>

/* Command line options */
static uint16_t vid = 0x18d1;			/* Google */
static uint16_t pid = 0x5022;			/* Hammer */
static uint8_t ep_num = 4;			/* console endpoint */
static char *firmware_binary = "144.0_2.0.bin";	/* firmware blob */

/* Firmware binary blob related */
#define FW_PAGE_SIZE			64
#define FW_PAGE_COUNT			768
#define FW_SIZE				(FW_PAGE_SIZE*FW_PAGE_COUNT)

static unsigned char fw_data[FW_SIZE];

/* Utility functions */
static int le_bytes_to_int(unsigned char *buf)
{
	return buf[0] + (int)(buf[1] << 8);
}

/* Command line parsing related */
static char *progname;
static char *short_opts = ":f:v:p:e:h";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"file",     1,   NULL, 'f'},
	{"vid",      1,   NULL, 'v'},
	{"pid",      1,   NULL, 'p'},
	{"ep",       1,   NULL, 'e'},
	{"help",     0,   NULL, 'h'},
	{NULL,       0,   NULL, 0},
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
	       "  -e,--ep     NUM         Endpoint (default %d)\n"
	       "  -h,--help               Show this message\n"
	       "\n", progname, firmware_binary, vid, pid, ep_num);

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

	opterr = 0;				/* quiet, you */
	while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
		switch (i) {
		case 'f':
			firmware_binary = optarg;
			break;
		case 'p':
			pid = (uint16_t) strtoul(optarg, &e, 16);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'v':
			vid = (uint16_t) strtoul(optarg, &e, 16);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'e':
			ep_num = (uint8_t) strtoul(optarg, &e, 0);
			if (!*optarg || (e && *e)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'h':
			usage(errorcnt);
			break;
		case 0:				/* auto-handled option */
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

	/* Read the FW file */
	FILE *f = fopen(firmware_binary, "rb");
	if (!(f && fread(fw_data, 1, FW_SIZE, f) == FW_SIZE)) {
		printf("Failed to read firmware from %s\n", firmware_binary);
		errorcnt++;
	}

	if (errorcnt)
		usage(errorcnt);

}

/* USB transfer related */
static unsigned char rx_buf[128];
static unsigned char tx_buf[128];

static struct libusb_device_handle *devh;
static struct libusb_transfer *rx_transfer;
static struct libusb_transfer *tx_transfer;

static int claimed_iface;
static int iface_num = -1;
static int tx_ready;
static int do_exit;

static void request_exit(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	do_exit++;    /* Why need this ? */

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
	exit(1);
}

#define DIE(msg, r)							\
	request_exit("%s: line %d, %s\n", msg, __LINE__,		\
		     libusb_error_name(r))

static void sighandler(int signum)
{
	request_exit("caught signal %d: %s\n", signum, sys_siglist[signum]);
}

static int find_interface_with_endpoint(int want_ep_num)
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
			for (k = 0; k < iface->bNumEndpoints; k++) {
				ep = &iface->endpoint[k];
				if (ep->bEndpointAddress == want_ep_num) {
					iface_num = i;
					break;
				}
			}
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

	iface_num = find_interface_with_endpoint(ep_num);
	if (iface_num < 0)
		request_exit("can't find interface owning EP %d\n", ep_num);

	printf("claim_interface %d to use endpoint %d\n", iface_num, ep_num);
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
#define I2C_PORT_ON_HAMMER		0x00
#define I2C_ADDRESS_ON_HAMMER		0x15

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

	if (r) {
		printf("Dumping the receive buffer:\n");
		printf("  Recv %d bytes from USB hosts.\n", actual);
		for (i = 0; i < actual; ++i)
			printf("    [%2d]bytes: 0x%0x\n", i, rx_buf[i]);
	}
	return r;
}

static int libusb_single_write_and_read(
		uint8_t *to_write, uint16_t write_length,
		uint8_t *to_read, uint8_t read_length)
{
	int r;
	int actual_length = -1;
	tx_transfer = rx_transfer = 0;

	memmove(tx_buf + 4, to_write, write_length);
	tx_buf[0] = I2C_PORT_ON_HAMMER;
	tx_buf[1] = I2C_ADDRESS_ON_HAMMER;
	tx_buf[2] = write_length;
	tx_buf[3] = read_length;
	tx_ready = 4 + write_length;

	r = libusb_bulk_transfer(devh,
			(ep_num | LIBUSB_ENDPOINT_OUT),
			tx_buf, tx_ready, &actual_length, 0);
	if (r == 0 && actual_length == tx_ready) {
		r = libusb_bulk_transfer(devh,
				(ep_num | LIBUSB_ENDPOINT_IN),
				rx_buf, 1024, &actual_length, 0);
	}
	return check_read_status(r, read_length, actual_length);
}

/* Control Elan trackpad I2C over USB */
#define ETP_I2C_INF_LENGTH		2

static int elan_write_and_read(
		int reg, unsigned char *buf, int read_length,
		int with_cmd, int cmd)
{

	tx_buf[0] = (reg >> 0) & 0xff;
	tx_buf[1] = (reg >> 8) & 0xff;
	if (with_cmd) {
		tx_buf[2] = (cmd >> 0) & 0xff;
		tx_buf[3] = (cmd >> 8) & 0xff;
	}
	return libusb_single_write_and_read(
			tx_buf, with_cmd ? 4 : 2, rx_buf, read_length);
}

static int elan_read_block(int reg, unsigned char *buf, int read_length)

	return elan_write_and_read(reg, buf, read_length, 0, 0);
}

static int elan_read_cmd(int reg)
{
	return elan_read_block(reg, rx_buf, ETP_I2C_INF_LENGTH);
}

/* Elan trackpad firmware information related */
#define ETP_I2C_IAP_VERSION_CMD		0x0110
#define ETP_I2C_FW_VERSION_CMD		0x0102
#define ETP_I2C_IAP_CHECKSUM_CMD	0x0315
#define ETP_I2C_FW_CHECKSUM_CMD		0x030F

static int elan_get_version(int is_iap)
{
	elan_read_cmd(
		is_iap ? ETP_I2C_IAP_VERSION_CMD : ETP_I2C_FW_VERSION_CMD);
	return le_bytes_to_int(rx_buf + 4);
}

static int elan_get_checksum(int is_iap)
{
	elan_read_cmd(
		is_iap ? ETP_I2C_IAP_CHECKSUM_CMD : ETP_I2C_FW_CHECKSUM_CMD);
	return le_bytes_to_int(rx_buf + 4);
}

static void get_fw_info(void)
{
	int iap_version = -1;
	int fw_version = -1;
	unsigned int iap_checksum = 0xffff;
	unsigned int fw_checksum = 0xffff;

	printf("Querying device info...\n");
	fw_checksum = elan_get_checksum(0);
	iap_checksum = elan_get_checksum(1);
	fw_version = elan_get_version(0);
	iap_version = elan_get_version(1);
	printf("IAP  version: %4x, FW  version: %4x\n",
			iap_version, fw_version);
	printf("IAP checksum: %4x, FW checksum: %4x\n",
			iap_checksum, fw_checksum);
}

int main(int argc, char *argv[])
{
	parse_cmdline(argc, argv);
	init_with_libusb();
	register_sigaction();
	get_fw_info();
	/* TODO(itspeter): Update the firmware */

	/* Print the updated firmware information */
	get_fw_info();
	return 0;
}
