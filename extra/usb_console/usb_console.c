/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
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

/* Options */
static uint16_t vid = 0x18d1;			/* Google */
static uint16_t pid = 0x500f;			/* discovery-stm32f072 */
static uint8_t ep_num = 4;			/* console endpoint */

static unsigned char rx_buf[1024];		/* much too big */
static unsigned char tx_buf[1024];		/* much too big */
static const struct libusb_pollfd **usb_fds;
static struct libusb_device_handle *devh;
static struct libusb_transfer *rx_transfer;
static struct libusb_transfer *tx_transfer;
static int tx_ready;
static int do_exit;

static void request_exit(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	do_exit++;
}

#define BOO(msg, r)							\
	request_exit("%s: line %d, %s\n", msg, __LINE__,		\
		     libusb_error_name(r))

static void sighandler(int signum)
{
	request_exit("caught signal %d: %s\n", signum, sys_siglist[signum]);
}

#if 0
static void show_xfer(const char *msg, struct libusb_transfer *t)
{
	printf("%s: f=%02x ep=%02x type=%d status=%d len=%d actlen=%d\n", msg,
	       t->flags,
	       t->endpoint, t->type, t->status, t->length, t->actual_length);
}
#endif

static void LIBUSB_CALL cb_rx(struct libusb_transfer *transfer)
{
	int r;

	if (transfer->actual_length) {
		transfer->buffer[transfer->actual_length] = '\0';
		fputs((char *)transfer->buffer, stdout);
		fflush(stdout);
	}

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		printf("rx_transfer cancelled\n");
		if (rx_transfer)
			libusb_free_transfer(rx_transfer);
		rx_transfer = NULL;
		return;
	}

	/* Try again */
	if (!do_exit) {
		r = libusb_submit_transfer(rx_transfer);
		if (r < 0)
			BOO("resubmit rx_transfer failed", r);
	}
}

static void LIBUSB_CALL cb_tx(struct libusb_transfer *transfer)
{
	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		if (tx_transfer)
			libusb_free_transfer(tx_transfer);
		tx_transfer = NULL;
		request_exit("tx_transfer cancelled\n");
		return;
	}

	if (tx_ready != transfer->actual_length)
		printf("%s: only sent %d/%d bytes\n", __func__,
		       transfer->actual_length, tx_ready);

	tx_ready = 0;
}

static void send_tx(int len)
{
	int r;

	libusb_fill_bulk_transfer(tx_transfer, devh,
				  ep_num, tx_buf, len, cb_tx, NULL, 0);

	r = libusb_submit_transfer(tx_transfer);
	if (r < 0)
		BOO("submit tx_transfer failed", r);
}

static void handle_stdin(void)
{
	static unsigned int i;
	int n;

	for (; i < sizeof(tx_buf) - 1; i++) {
		n = read(0, tx_buf + i, 1);
		if (n == 0) {
			request_exit("EOF on stdin\n");
			return;
		}
		if (n < 0) {
			request_exit("stdin: %s\n", strerror(errno));
			return;
		}

		if (tx_buf[i] == '\n') {
			i++;
			tx_buf[i] = '\0';
			break;
		}
	}

	tx_ready = strlen((char *)tx_buf) + 1;
	send_tx(tx_ready);
	i = 0;
}

static void handle_libusb(void)
{
	struct timeval tv = { 0, 0 };
	int r;

	r = libusb_handle_events_timeout_completed(NULL, &tv, &do_exit);
	if (r < 0)
		BOO("libusb event problem", r);
}

static int wait_for_stuff_to_happen(void)
{
	int i, r, nfds = 0;
	fd_set readset, writeset;
	struct timeval tv = { 1, 0 };

	if (!usb_fds) {
		request_exit("No usb_fds to watch\n");
		return -1;
	}

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	/* always watch stdin */
	FD_SET(0, &readset);

	for (i = 0; usb_fds[i]; i++) {
		int fd = usb_fds[i]->fd;
		short events = usb_fds[i]->events;
		if (fd > nfds)
			nfds = fd;

		if (events & POLLIN)
			FD_SET(fd, &readset);
		if (events & POLLOUT)
			FD_SET(fd, &writeset);
	}

	r = select(nfds + 1, &readset, &writeset, NULL, &tv);
	if (r < 0) {
		request_exit("select: %s\n", strerror(errno));
		return -1;
	}

	if (r == 0)				/* timed out */
		return 0;

	/* Ignore stdin until we've finished sending the current line */
	if (!tx_ready && FD_ISSET(0, &readset))
		return 1;

	/* libusb, then */
	return 2;
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
		BOO("get_active_config", r);
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

static char *progname;
static char *short_opts = ":v:p:e:h";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
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
	       "A very simple serial console emulator\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "  -v,--vid    HEXVAL      Vendor ID (default %04x)\n"
	       "  -p,--pid    HEXVAL      Product ID (default %04x)\n"
	       "  -e,--ep     NUM         Endpoint (default %d)\n"
	       "  -h,--help               Show this message\n"
	       "\n", progname, vid, pid, ep_num);

	exit(!!errs);
}

int main(int argc, char *argv[])
{
	struct sigaction sigact;
	int iface_num;
	int claimed_iface = 0;
	int r = 1;
	int errorcnt = 0;
	char *e = 0;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	opterr = 0;				/* quiet, you */
	while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
		switch (i) {
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

	if (errorcnt)
		usage(errorcnt);

	printf("init\n");
	r = libusb_init(NULL);
	if (r < 0) {
		BOO("init", r);
		exit(1);
	}

	printf("open_device %04x:%04x\n", vid, pid);
	devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!devh) {
		printf("can't find device\n");
		goto out;
	}

	iface_num = find_interface_with_endpoint(ep_num);
	if (iface_num < 0) {
		printf("can't find interface owning EP %d\n", ep_num);
		goto out;
	}
	/* NOTE: The EP might be on an alternate interface. We should switch
	 * to the correct one. */

	printf("claim_interface %d to use endpoint %d\n", iface_num, ep_num);
	r = libusb_claim_interface(devh, iface_num);
	if (r < 0) {
		BOO("claim interface", r);
		goto out;
	}
	claimed_iface = 1;

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);

	printf("alloc_transfers\n");
	rx_transfer = libusb_alloc_transfer(0);
	if (!rx_transfer) {
		printf("can't alloc rx_transfer");
		goto out;
	}
	libusb_fill_bulk_transfer(rx_transfer, devh,
				  0x80 | ep_num,
				  rx_buf, sizeof(rx_buf), cb_rx, NULL, 0);

	tx_transfer = libusb_alloc_transfer(0);
	if (!tx_transfer) {
		printf("can't alloc tx_transfer");
		goto out;
	}

	printf("get_pollfds\n");
	usb_fds = libusb_get_pollfds(NULL);
	if (!usb_fds) {
		printf("can't get usb_fds\n");
		goto out;
	}

	printf("submit rx_transfer\n");
	r = libusb_submit_transfer(rx_transfer);
	if (r < 0) {
		BOO("submit rx_transfer", r);
		goto out;
	}

	printf("READY\n-------\n");
	while (!do_exit) {
		r = wait_for_stuff_to_happen();
		switch (r) {
		case 0:	/* timed out */
			/* printf("."); */
			/* fflush(stdout); */
			break;
		case 1:	/* stdin ready */
			handle_stdin();
			break;
		case 2:	/* libusb ready */
			handle_libusb();
			break;
		}
	}

	printf("-------\nshutting down\n");

	r = libusb_cancel_transfer(rx_transfer);
	if (r < 0) {
		BOO("cancel rx_transfer", r);
		if (rx_transfer)
			libusb_free_transfer(rx_transfer);
		rx_transfer = 0;
	}

	if (tx_ready) {
		r = libusb_cancel_transfer(tx_transfer);
		if (r < 0) {
			BOO("cancel tx_transfer", r);
			if (tx_transfer)
				libusb_free_transfer(tx_transfer);
			tx_transfer = 0;
		}
	}

	while (rx_transfer) {
		printf("draining events...\n");
		r = libusb_handle_events(NULL);
		if (r < 0) {
			printf("Huh: %s\n", libusb_error_name(r));
			break;
		}
	}

	printf("bye\n");
	r = 0;
 out:
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

	return r;
}
