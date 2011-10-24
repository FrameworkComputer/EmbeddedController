/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ec_uartd.c - UART daemon for serial output from a FTDI FT2232 chip
 *
 * based on chromeos_public/src/third_party/hdctools/src/ftdiuart.c
 *
 * compile with:
 *    gcc -o ec_uartd ec_uartd.c -lftdi
 */

#include <fcntl.h>
#include <ftdi.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

int grantpt(int fd);
int unlockpt(int fd);
int ptsname_r(int fd, char *buf, size_t buflen);
int posix_openpt(int flags);


/* Create a pty.  Returns the pty handle, which should be closed with
 * close(), or -1 if error. */
int openpty(const char* desc) {
  char ptname[PATH_MAX];
  struct termios tty_cfg;
  int fd;

  if ((fd = posix_openpt(O_RDWR | O_NOCTTY)) == -1) {
    perror("opening pty master");
    return -1;
  }
  if (grantpt(fd) == -1) {
    perror("grantpt");
    return -1;
  }
  if (unlockpt(fd) == -1) {
    perror("unlockpt");
    return -1;
  }
  if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
    perror("fcntl setfl -> nonblock");
    return -1;
  }
  if (ptsname_r(fd, ptname, PATH_MAX) != 0) {
    perror("getting name of pty");
    return -1;
  }
  fprintf(stderr, "%s pty name = %s\n", desc, ptname);
  if (!isatty(fd)) {
    perror("not a TTY device\n");
    return -1;
  }
  cfmakeraw(&tty_cfg);
  tcsetattr(fd, TCSANOW, &tty_cfg);
  if (chmod(ptname, 0666) == -1) {
    perror("setting pty attributes");
    return -1;
  }

  return fd;
}


int main(int argc, char **argv) {
  struct ftdi_context fcontext;
  unsigned char buf[1024], buf_ec[1024], buf_x86[1024];
  int fd_ec, fd_x86;
  int rv, i;

  // Init
  if (ftdi_init(&fcontext) < 0)
  {
    fprintf(stderr, "ftdi_init failed\n");
    return 1;
  }

  // Open interface B (UART) in the FTDI device and set 115kbaud
  ftdi_set_interface(&fcontext, INTERFACE_B);
  rv = ftdi_usb_open(&fcontext, 0x0403, 0xbcda);
  if (rv < 0)
  {
    fprintf(stderr, "error opening ftdi device: %d (%s)\n",
            rv, ftdi_get_error_string(&fcontext));
    return 2;
  }
  rv = ftdi_set_baudrate(&fcontext, 115200);
  if (rv < 0)
  {
    fprintf(stderr, "error setting baudrate: %d (%s)\n",
            rv, ftdi_get_error_string(&fcontext));
    return 2;
  }
  // Set DTR; this muxes RX on the ICDI board
  ftdi_setdtr(&fcontext, 1);

  // Open the ptys
  fd_ec = openpty("EC");
  fd_x86 = openpty("x86");
  if (fd_ec == -1 || fd_x86 == -1)
    return 3;

  // Read and write data forever
  while (1) {
    int bytes;
    int bytes_ec = 0;
    int bytes_x86 = 0;

    // Copy data from EC pty, turning high bit on
    if ((bytes = read(fd_ec, buf, sizeof(buf))) > 0) {
      for (i = 0; i < bytes; i++)
        buf[i] |= 0x80;

      rv = ftdi_write_data(&fcontext, buf, bytes);
      if (rv != bytes) {
        perror("writing to uart");
        break;
      }
    }

    // Copy data from x86 pty, turning high bit off
    if ((bytes = read(fd_x86, buf, sizeof(buf))) > 0) {
      for (i = 0; i < bytes; i++)
        buf[i] &= ~0x80;

      rv = ftdi_write_data(&fcontext, buf, bytes);
      if (rv != bytes) {
        perror("writing to uart");
        break;
      }
    }

    usleep(1000);

    // Get output from UART
    bytes = ftdi_read_data(&fcontext, buf, sizeof(buf));
    if (bytes < 0) {
      perror("failed ftdi_read_data");
      break;
    }

    // Split into EC and x86 buffers
    for (i = 0; i < bytes; i++) {
      if (buf[i] & 0x80)
        buf_ec[bytes_ec++] = buf[i] & ~0x80;
      else
        buf_x86[bytes_x86++] = buf[i];
    }

    // Copy data to EC pty
    if (bytes_ec > 0) {
      int bytes_remaining = bytes_ec;
      while ((bytes = write(fd_ec, buf_ec, bytes_remaining)) > 0) {
        bytes_remaining -= bytes;
      }
      if (bytes == -1) {
        perror("writing ftdi data to EC pty");
      }
    }

    // Copy data to x86 pty
    if (bytes_x86 > 0) {
      int bytes_remaining = bytes_x86;
      while ((bytes = write(fd_x86, buf_x86, bytes_remaining)) > 0) {
        bytes_remaining -= bytes;
      }
      if (bytes == -1) {
        perror("writing ftdi data to x86 pty");
      }
    }
  }

  // Cleanup
  close(fd_ec);
  close(fd_x86);
  ftdi_usb_close(&fcontext);
  ftdi_deinit(&fcontext);
  return 0;
}
